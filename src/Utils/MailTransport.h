#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>

namespace astra_mail { namespace net {
struct Settings {
    unsigned dnsMs = 10000, connectMs = 10000;
    unsigned smtpCommandMs = 300000, smtpDataStartMs = 120000, smtpFinalMs = 600000;
    unsigned ioIdleMs = 180000, imapCommandMs = 300000, imapAppendMs = 600000;
    unsigned keepIdleSeconds = 30, keepIntervalSeconds = 10, keepProbes = 3, tcpUserMs = 60000;
};
struct SocketState;
class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    operator int() const;
    void reset();
private:
    explicit Socket(std::shared_ptr<SocketState> state);
    std::shared_ptr<SocketState> state_;
    friend Socket connectSocket(const std::string&, int, std::string&);
};
void configure(const Settings& settings);
Settings currentSettings();
Settings settingsFor(int fd);
Socket connectSocket(const std::string& host, int port, std::string& error);
void beginResponse(int fd, unsigned timeoutMs);
ssize_t receive(int fd, void* data, size_t size, bool literal = false);
bool sendAll(int fd, const void* data, size_t size);
void sendBestEffort(int fd, const std::string& data);
void requestStop();
void resetStop();
bool stopping();
const std::string& lastError();
}}
