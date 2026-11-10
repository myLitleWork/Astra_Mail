#include "MailDeliveryState.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace astra_mail {
namespace {
constexpr size_t maxStateSize = 1024 * 1024;
bool validAddress(const std::string& value) {
    if (value.empty() || value.size() > 512) return false;
    for (unsigned char c : value) if (c <= 32 || c == 127 || c == '<' || c == '>') return false;
    return true;
}
bool writeAll(int fd, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}
}

fs::path MailDeliveryState::path(const fs::path& folder) {
    return folder / ".astramail-delivery-state";
}
bool MailDeliveryState::load(const fs::path& folder, std::string& error) {
    *this = MailDeliveryState{};
    const int fd = ::open(path(folder).string().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return true;
        error = "Не удалось открыть состояние отправки"; return false;
    }
    struct stat st{};
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 &&
              static_cast<uint64_t>(st.st_size) <= maxStateSize;
    std::string bytes;
    char buffer[4096];
    while (ok) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (!n) break;
        bytes.append(buffer, static_cast<size_t>(n));
        if (bytes.size() > maxStateSize) ok = false;
    }
    ::close(fd);
    std::istringstream input(bytes);
    std::string version;
    size_t count = 0;
    if (!ok || !(input >> version >> std::quoted(context) >> std::quoted(fingerprint) >> count) ||
        version != "ASTRAMAIL_DELIVERY_1" || context.empty() || fingerprint.empty() || !count || count > 4096) {
        error = "Состояние отправки повреждено: автоматический повтор запрещён"; return false;
    }
    std::set<std::string> unique;
    for (size_t i = 0; i < count; ++i) {
        char status = 0;
        RecipientProgress entry;
        if (!(input >> status >> std::quoted(entry.address)) || !validAddress(entry.address) ||
            !unique.insert(entry.address).second || (status != 'P' && status != 'D' && status != 'U')) {
            error = "Некорректные адресаты в состоянии отправки"; return false;
        }
        entry.status = status == 'D' ? RecipientDelivery::Delivered :
                       status == 'U' ? RecipientDelivery::Unknown : RecipientDelivery::Pending;
        recipients.push_back(entry);
    }
    input >> std::ws;
    if (!input.eof()) { error = "Лишние данные в состоянии отправки"; return false; }
    exists = true; return true;
}

bool MailDeliveryState::save(const fs::path& folder, std::string& error) const {
    if (recipients.empty() || recipients.size() > 4096 || context.empty() || fingerprint.empty()) {
        error = "Некорректное состояние отправки"; return false;
    }
    std::ostringstream output;
    output << "ASTRAMAIL_DELIVERY_1\n" << std::quoted(context) << '\n'
           << std::quoted(fingerprint) << '\n' << recipients.size() << '\n';
    std::set<std::string> unique;
    for (const auto& entry : recipients) {
        if (!validAddress(entry.address) || !unique.insert(entry.address).second) {
            error = "Некорректный или повторный адресат"; return false;
        }
        const char status = entry.status == RecipientDelivery::Delivered ? 'D' :
                            entry.status == RecipientDelivery::Unknown ? 'U' : 'P';
        output << status << ' ' << std::quoted(entry.address) << '\n';
    }
    const auto bytes = output.str();
    if (bytes.size() > maxStateSize) { error = "Слишком большой список адресатов"; return false; }
    // fsync file -> atomic rename -> fsync directory, before any SMTP DATA.
    std::string name = (folder / ".astramail-delivery-state.tmp.XXXXXX").string();
    std::vector<char> temp(name.begin(), name.end()); temp.push_back(0);
    const int fd = ::mkstemp(temp.data());
    if (fd < 0) { error = "Не удалось создать состояние отправки"; return false; }
    bool ok = writeAll(fd, bytes) && ::fsync(fd) == 0;
    if (::close(fd) != 0) ok = false;
    if (ok) ok = ::rename(temp.data(), path(folder).string().c_str()) == 0;
    if (!ok) {
        ::unlink(temp.data()); error = "Не удалось сохранить состояние отправки"; return false;
    }
    const int dir = ::open(folder.string().c_str(), O_RDONLY | O_DIRECTORY);
    ok = dir >= 0 && ::fsync(dir) == 0;
    if (dir >= 0) ::close(dir);
    if (!ok) error = "Не подтверждена запись состояния отправки на диск";
    return ok;
}

bool MailDeliveryState::complete() const {
    return !recipients.empty() && std::all_of(recipients.begin(), recipients.end(),
        [](const RecipientProgress& entry) { return entry.status == RecipientDelivery::Delivered; });
}
std::vector<std::string> MailDeliveryState::pending() const {
    std::vector<std::string> result;
    for (const auto& entry : recipients)
        if (entry.status != RecipientDelivery::Delivered) result.push_back(entry.address);
    return result;
}
std::string MailDeliveryState::summary() const {
    std::string result;
    for (const auto& entry : recipients) {
        if (!result.empty()) result += "; ";
        result += entry.address + ": " + (entry.status == RecipientDelivery::Delivered ? "принято сервером" :
                   entry.status == RecipientDelivery::Unknown ? "приём не подтверждён, будет автоповтор (возможен дубль)" : "ожидает отправки");
    }
    return result;
}

MailDeliveryLock::MailDeliveryLock(const fs::path& folder) {
    fd_ = ::open((folder / ".astramail-delivery-lock").string().c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd_ < 0) return;
    struct stat st{};
    if (::fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || ::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_); fd_ = -1;
    }
}
MailDeliveryLock::~MailDeliveryLock() { if (fd_ >= 0) ::close(fd_); }

bool mailPayloadFingerprint(const MailFolderInfo& info, std::string& fingerprint, std::string& error) {
    // Content identity for accidental edits, not a cryptographic authentication tag.
    uint64_t hash = 14695981039346656037ULL;
    auto feed = [&](unsigned char c) { hash ^= c; hash *= 1099511628211ULL; };
    auto attachments = info.attachments;
    std::sort(attachments.begin(), attachments.end(),
        [](const AttachmentInfo& a, const AttachmentInfo& b) { return a.name < b.name; });
    for (const auto& attachment : attachments) {
        for (unsigned char c : attachment.name) feed(c);
        feed(0);
        std::ifstream file(attachment.fullPath, std::ios::binary);
        if (!file) { error = "Не удалось прочитать вложение для состояния отправки"; return false; }
        char buffer[8192];
        while (file) {
            file.read(buffer, sizeof(buffer));
            const auto n = file.gcount();
            for (std::streamsize i = 0; i < n; ++i) feed(static_cast<unsigned char>(buffer[i]));
        }
        if (!file.eof()) { error = "Ошибка чтения вложения"; return false; }
        // Include a boundary and byte count to distinguish adjacent attachments.
        feed(0); for (unsigned char c : std::to_string(attachment.sizeBytes)) feed(c); feed(0);
    }
    fingerprint = std::to_string(hash); return true;
}
}
