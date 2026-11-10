#include "SecretInput.h"
#include "SensitiveString.h"
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <iostream>
#include <mutex>

namespace astra_mail {
namespace {
std::mutex inputMutex;
constexpr int terminationSignals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
// Static storage is required because a signal may be delivered to another
// thread. Only default terminating dispositions are wrapped, never custom ones.
termios savedTerminal;
volatile sig_atomic_t terminalFd = -1;
void restoreAndTerminate(int signal) {
    const int savedErrno = errno;
    const int fd = terminalFd;
    if (fd >= 0) while (::tcsetattr(fd, TCSANOW, &savedTerminal) != 0 && errno == EINTR) {}
    struct sigaction original{};
    original.sa_handler = SIG_DFL;
    ::sigemptyset(&original.sa_mask);
    ::sigaction(signal, &original, nullptr);
    ::raise(signal);
    errno = savedErrno;
}
class TerminalEcho {
public:
    bool hide() {
        if (!::isatty(STDIN_FILENO)) return errno == ENOTTY;
        if (::tcgetattr(STDIN_FILENO, &savedTerminal) != 0) return false;
        terminalFd = STDIN_FILENO;
        for (size_t i = 0; i < 4; ++i) {
            if (::sigaction(terminationSignals[i], nullptr, &previous_[i]) != 0) return false;
            if (previous_[i].sa_handler != SIG_DFL) continue;
            struct sigaction action{};
            action.sa_handler = restoreAndTerminate;
            ::sigemptyset(&action.sa_mask);
            if (::sigaction(terminationSignals[i], &action, nullptr) != 0) return false;
            installed_[i] = true;
        }
        auto hidden = savedTerminal;
        hidden.c_lflag &= ~(ECHO | ECHONL);
        changed_ = true;
        int status;
        do { status = ::tcsetattr(STDIN_FILENO, TCSANOW, &hidden); } while (status != 0 && errno == EINTR);
        if (status != 0) return false;
        termios actual{};
        return ::tcgetattr(STDIN_FILENO, &actual) == 0 && !(actual.c_lflag & (ECHO | ECHONL));
    }
    bool restore() {
        bool ok = true;
        if (changed_) {
            int status;
            do { status = ::tcsetattr(STDIN_FILENO, TCSANOW, &savedTerminal); } while (status != 0 && errno == EINTR);
            ok = status == 0;
            if (ok) changed_ = false;
        }
        terminalFd = -1;
        for (size_t i = 0; i < 4; ++i) if (installed_[i]) {
            ::sigaction(terminationSignals[i], &previous_[i], nullptr);
            installed_[i] = false;
        }
        return ok;
    }
    ~TerminalEcho() { restore(); }
private:
    bool changed_ = false;
    bool installed_[4]{};
    struct sigaction previous_[4]{};
};
}

bool readSecret(const char* prompt, std::string& value, size_t maximum) {
    std::lock_guard<std::mutex> lock(inputMutex);
    clearSensitiveString(value);
    TerminalEcho terminal;
    if (!terminal.hide()) {
        std::cout << "Не удалось включить скрытый ввод. Ввод отменён.\n";
        return false;
    }
    // Hide echo before publishing the prompt (also important over SSH).
    std::cout << prompt << std::flush;
    bool complete = false, oversized = false;
    try {
        char c;
        while (std::cin.get(c)) {
            if (c == '\n') { complete = true; break; }
            if (value.size() < maximum) value.push_back(c);
            else oversized = true; // Drain the line without retaining more secret data.
        }
        if (!terminal.restore()) complete = false;
    } catch (...) {
        clearSensitiveString(value);
        throw; // TerminalEcho restores the terminal during unwinding.
    }
    std::cout << '\n';
    if (!complete || oversized || value.empty()) {
        clearSensitiveString(value);
        std::cout << "Ввод отменён: пустая, незавершённая или слишком длинная строка.\n";
        return false;
    }
    return true;
}

bool validNewPin(const std::string& value) {
    if (value.empty() || value.size() > 1024) return false;
    for (unsigned char c : value) if (c <= 32 || c == 127) return false;
    // Unicode whitespace which can look like an ordinary space in a UTF-8 TTY.
    for (const char* space : {"\xC2\x85", "\xC2\xA0", "\xE1\x9A\x80", "\xE2\x80\x80",
         "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
         "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A",
         "\xE2\x80\xA8", "\xE2\x80\xA9", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80"})
        if (value.find(space) != std::string::npos) return false;
    return true;
}
}
