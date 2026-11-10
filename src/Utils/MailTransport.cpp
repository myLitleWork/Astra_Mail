#include "MailTransport.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace astra_mail { namespace net {
using Clock = std::chrono::steady_clock;
static int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
struct SocketState {
    std::atomic<int> fd{-1};
    std::atomic<int64_t> responseDeadline{0};
    Settings settings;
};
namespace {
struct Registry {
    std::mutex mutex;
    std::unordered_map<int, std::shared_ptr<SocketState>> sockets;
    Settings settings;
    std::atomic<bool> stop{false};
};
// Alive until process termination: socket/Idle singleton destructors can run in
// either order. Closed sockets are removed; no connection history accumulates.
Registry& registry() { static Registry* value = new Registry; return *value; }
thread_local std::string errorText;
bool fail(int code, const char* stage) {
    errno = code;
    errorText = code == ECANCELED ? "Сетевая операция отменена" :
        std::string(stage) + ": " + (code == ETIMEDOUT ? "истекло время ожидания" : std::strerror(code));
    return false;
}
std::shared_ptr<SocketState> find(int fd) {
    auto& r = registry(); std::lock_guard<std::mutex> lock(r.mutex);
    const auto it = r.sockets.find(fd);
    return it == r.sockets.end() ? nullptr : it->second;
}
bool waitFd(int fd, short events, int64_t deadline) {
    while (!stopping()) {
        const int64_t remaining = deadline - nowMs();
        if (remaining <= 0) return fail(ETIMEDOUT, "Связь");
        pollfd descriptor{}; descriptor.fd = fd; descriptor.events = events;
        const int result = ::poll(&descriptor, 1, static_cast<int>(std::min<int64_t>(remaining, 100)));
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) return fail(errno, "Ожидание сети");
        if (result > 0) {
            if (descriptor.revents & POLLNVAL) return fail(EBADF, "Сокет");
            return true; // HUP/ERR is interpreted by recv/send/SO_ERROR.
        }
    }
    return fail(ECANCELED, "Связь");
}
struct Address { sockaddr_storage data{}; socklen_t size = 0; int family = 0, type = SOCK_STREAM, protocol = 0; };
struct ResolverPool { std::atomic<unsigned> active{0}; };
struct Resolution {
    std::mutex mutex; std::condition_variable ready;
    bool done = false; int result = EAI_FAIL;
    std::vector<Address> addresses;
};
bool resolve(const std::string& host, int port, unsigned timeout, std::vector<Address>& addresses) {
    Address numeric;
    auto v4 = reinterpret_cast<sockaddr_in*>(&numeric.data);
    if (::inet_pton(AF_INET, host.c_str(), &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET; v4->sin_port = htons(port);
        numeric.family = AF_INET; numeric.size = sizeof(sockaddr_in); addresses.push_back(numeric); return true;
    }
    auto v6 = reinterpret_cast<sockaddr_in6*>(&numeric.data);
    if (::inet_pton(AF_INET6, host.c_str(), &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6; v6->sin6_port = htons(port);
        numeric.family = AF_INET6; numeric.size = sizeof(sockaddr_in6); addresses.push_back(numeric); return true;
    }
    static const auto pool = std::make_shared<ResolverPool>();
    const auto sharedPool = pool;
    const auto result = std::make_shared<Resolution>();
    if (sharedPool->active.fetch_add(1) >= 4) {
        sharedPool->active.fetch_sub(1); return fail(EAGAIN, "Занят DNS resolver");
    }
    try {
        std::thread([result, sharedPool, host, port]() {
            addrinfo hints{}, *list = nullptr; hints.ai_socktype = SOCK_STREAM;
            int status = EAI_MEMORY;
            std::vector<Address> found;
            try {
                status = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list);
                if (!status) for (auto p = list; p; p = p->ai_next) {
                    if (p->ai_addrlen > sizeof(sockaddr_storage)) continue;
                    Address entry; std::memcpy(&entry.data, p->ai_addr, p->ai_addrlen);
                    entry.size = p->ai_addrlen; entry.family = p->ai_family;
                    entry.type = p->ai_socktype; entry.protocol = p->ai_protocol; found.push_back(entry);
                }
            } catch (...) { status = EAI_MEMORY; }
            if (list) ::freeaddrinfo(list);
            { std::lock_guard<std::mutex> lock(result->mutex); result->result = status;
              result->addresses = std::move(found); result->done = true; }
            sharedPool->active.fetch_sub(1); result->ready.notify_all();
        }).detach();
    } catch (...) { sharedPool->active.fetch_sub(1); return fail(EAGAIN, "Запуск DNS resolver"); }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
    std::unique_lock<std::mutex> lock(result->mutex);
    while (!result->done && !stopping()) {
        if (Clock::now() >= deadline) return fail(ETIMEDOUT, "DNS");
        result->ready.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(100)));
    }
    if (stopping()) return fail(ECANCELED, "DNS");
    if (result->result || result->addresses.empty()) { errorText = "DNS: " + std::string(gai_strerror(result->result)); return false; }
    addresses = std::move(result->addresses); return true;
}
void tune(int fd, const Settings& settings) {
    int enabled = 1; ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
    int idle = settings.keepIdleSeconds, interval = settings.keepIntervalSeconds, probes = settings.keepProbes;
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
#ifdef TCP_USER_TIMEOUT
    const unsigned timeout = settings.tcpUserMs;
    ::setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout));
#endif
}
}

Socket::Socket(std::shared_ptr<SocketState> state) : state_(std::move(state)) {}
Socket::~Socket() { reset(); }
Socket::Socket(Socket&& other) noexcept : state_(std::move(other.state_)) {}
Socket& Socket::operator=(Socket&& other) noexcept { if (this != &other) { reset(); state_ = std::move(other.state_); } return *this; }
Socket::operator int() const { return state_ ? state_->fd.load() : -1; }
void Socket::reset() {
    if (!state_) return;
    auto& r = registry();
    { std::lock_guard<std::mutex> lock(r.mutex);
      const int fd = state_->fd.exchange(-1);
      if (fd >= 0) { r.sockets.erase(fd); ::close(fd); } }
    state_.reset();
}
void configure(const Settings& value) { auto& r = registry(); std::lock_guard<std::mutex> lock(r.mutex); r.settings = value; }
Settings currentSettings() { auto& r = registry(); std::lock_guard<std::mutex> lock(r.mutex); return r.settings; }
Settings settingsFor(int fd) { const auto state = find(fd); return state ? state->settings : currentSettings(); }
const std::string& lastError() { return errorText; }
bool stopping() { return registry().stop.load(); }
void resetStop() { registry().stop.store(false); }
void requestStop() {
    auto& r = registry(); r.stop.store(true);
    std::lock_guard<std::mutex> lock(r.mutex);
    for (const auto& pair : r.sockets) ::shutdown(pair.first, SHUT_RDWR);
}

Socket connectSocket(const std::string& host, int port, std::string& error) {
    error.clear(); errorText.clear();
    if (stopping()) { fail(ECANCELED, "Подключение"); error = errorText; return {}; }
    if (port < 1 || port > 65535 || host.empty()) { error = "Некорректный сервер/порт"; return {}; }
    const auto settings = currentSettings();
    std::vector<Address> addresses;
    if (!resolve(host, port, settings.dnsMs, addresses)) { error = errorText; return {}; }
    const int64_t deadline = nowMs() + settings.connectMs;
    for (const auto& address : addresses) {
        const auto state = std::make_shared<SocketState>(); state->settings = settings;
        const int fd = ::socket(address.family, address.type, address.protocol);
        if (fd < 0) continue;
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) { ::close(fd); continue; }
        tune(fd, settings);
        state->fd.store(fd);
        try { auto& r = registry(); std::lock_guard<std::mutex> lock(r.mutex); r.sockets.emplace(fd, state); }
        catch (...) { ::close(fd); state->fd.store(-1); throw; }
        Socket connection(state);
        int status = ::connect(fd, reinterpret_cast<const sockaddr*>(&address.data), address.size);
        if (status != 0 && errno == EINPROGRESS && waitFd(fd, POLLOUT, deadline)) {
            int code = 0; socklen_t size = sizeof(code);
            status = ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &size) == 0 && !code ? 0 : -1;
            if (status) fail(code ? code : errno, "Подключение");
        } else if (status != 0 && errno != EINPROGRESS) fail(errno, "Подключение");
        if (!status && !stopping()) { beginResponse(fd, settings.imapCommandMs); return connection; }
        if (stopping()) { fail(ECANCELED, "Подключение"); break; }
    }
    error = errorText.empty() ? "Подключение не удалось" : errorText;
    return {};
}
void beginResponse(int fd, unsigned timeoutMs) {
    errorText.clear();
    const auto state = find(fd);
    if (state) state->responseDeadline.store(nowMs() + std::max(1u, timeoutMs));
}
ssize_t receive(int fd, void* data, size_t size, bool literal) {
    const auto state = find(fd);
    if (!state || fd < 0) { fail(EBADF, "Сокет"); return -1; }
    const auto deadline = literal ? nowMs() + state->settings.ioIdleMs : state->responseDeadline.load();
    while (waitFd(fd, POLLIN, deadline)) {
        if (stopping()) { fail(ECANCELED, "Чтение"); return -1; }
        const ssize_t n = ::recv(fd, data, size, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) fail(errno, "Чтение");
        if (!n) errorText = "Сервер закрыл соединение";
        return n;
    }
    return -1;
}
bool sendAll(int fd, const void* data, size_t size) {
    errorText.clear();
    const auto state = find(fd);
    if (!state) return fail(EBADF, "Сокет");
    auto p = static_cast<const char*>(data);
    int64_t deadline = nowMs() + state->settings.ioIdleMs;
    while (size) {
        if (stopping()) return fail(ECANCELED, "Передача");
        if (nowMs() >= deadline) return fail(ETIMEDOUT, "Передача");
        const ssize_t n = ::send(fd, p, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { if (!waitFd(fd, POLLOUT, deadline)) return false; continue; }
        if (n <= 0) return fail(n < 0 ? errno : EPIPE, "Передача");
        p += n; size -= static_cast<size_t>(n); deadline = nowMs() + state->settings.ioIdleMs;
    }
    return true;
}
void sendBestEffort(int fd, const std::string& data) {
    if (fd >= 0 && !stopping()) ::send(fd, data.data(), data.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}
}}
