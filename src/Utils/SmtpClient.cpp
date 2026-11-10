#include "SmtpClient.h"
#include "SensitiveString.h"
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
bool sendAll(int sock, const char* data, size_t size) {
    return astra_mail::net::sendAll(sock, data, size);
}
bool validAddress(const std::string& address) {
    if (address.empty() || address.size() > 512) return false;
    for (unsigned char c : address)
        if (c <= 32 || c == 127 || c == '<' || c == '>') return false;
    return true;
}
// Normalize line endings and dot-stuff every line, including across fread boundaries.
bool sendBody(int sock, FILE* file) {
    std::string buffer;
    bool start = true, cr = false;
    auto flush = [&]() {
        if (!sendAll(sock, buffer.data(), buffer.size())) return false;
        buffer.clear(); return true;
    };
    auto newline = [&]() { buffer += "\r\n"; start = true; };
    char chunk[8192];
    size_t count;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) != 0) {
        for (size_t i = 0; i < count; ++i) {
            const char c = chunk[i];
            if (cr) { newline(); cr = false; if (c == '\n') continue; }
            if (c == '\r') { cr = true; continue; }
            if (c == '\n') { newline(); continue; }
            if (start && c == '.') buffer += '.';
            buffer += c; start = false;
            if (buffer.size() >= 16384 && !flush()) return false;
        }
        if (buffer.size() >= 8192 && !flush()) return false;
    }
    if (std::ferror(file)) return false;
    if (cr || !start) newline();
    return flush();
}
}

SmtpClient::SmtpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

SmtpClient::~SmtpClient() { astra_mail::clearSensitiveString(pass_); }

void SmtpClient::setCredentials(const std::string& user, const std::string& password) {
    user_ = user; pass_ = password;
}

void SmtpClient::abortSession(Session& ses) {
    ses.sock.reset(); ses.replyBuffer.clear();
}

bool SmtpClient::openSession(Session& ses, std::string& error) {
    abortSession(ses);
    error.clear();
    ses.sock = socketConnect(error);
    if (ses.sock < 0) return false;
    int code = 0;
    std::string reply;
    auto step = [&](const char* stage, const std::string& command, int expected) {
        reply.clear();
        if ((!command.empty() && !sendLine(ses.sock, command)) ||
            !recvResponse(ses, code, reply) || code != expected) {
            error = std::string(stage) + ": " + (!astra_mail::net::lastError().empty() ? astra_mail::net::lastError() :
                (reply.empty() ? "connection/protocol error" : reply));
            abortSession(ses); return false;
        }
        return true;
    };
    return step("Greeting", "", 220) && step("EHLO", "EHLO astra", 250) &&
           step("AUTH", "AUTH LOGIN", 334) && step("AUTH user", base64(user_), 334) &&
           step("AUTH password", base64(pass_), 235);
}

void SmtpClient::closeSession(Session& ses) {
    astra_mail::net::sendBestEffort(ses.sock, "QUIT\r\n");
    abortSession(ses);
}

bool SmtpClient::sendLine(int sock, const std::string& line) {
    const std::string wire = line + "\r\n";
    return sendAll(sock, wire.data(), wire.size());
}

bool SmtpClient::recvResponse(Session& ses, int& code, std::string& reply, unsigned timeoutMs) {
    code = 0; reply.clear();
    astra_mail::net::beginResponse(ses.sock, timeoutMs ? timeoutMs : astra_mail::net::settingsFor(ses.sock).smtpCommandMs);
    for (size_t lines = 0; lines < 128; ++lines) {
        size_t end;
        while ((end = ses.replyBuffer.find("\r\n")) == std::string::npos) {
            if (ses.replyBuffer.size() > 8192) return false;
            char buffer[2048];
            const ssize_t n = astra_mail::net::receive(ses.sock, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            ses.replyBuffer.append(buffer, static_cast<size_t>(n));
        }
        const std::string line = ses.replyBuffer.substr(0, end);
        ses.replyBuffer.erase(0, end + 2);
        if (line.size() < 3 || line.size() > 8192 || reply.size() + line.size() > 65536)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (line[i] < '0' || line[i] > '9') return false;
        const int current = (line[0]-'0')*100 + (line[1]-'0')*10 + line[2]-'0';
        if (line[0] < '2' || line[0] > '5' || line[1] > '5' || (code && code != current) ||
            (line.size() > 3 && line[3] != ' ' && line[3] != '-')) return false;
        code = current;
        if (!reply.empty()) reply += " | ";
        reply += line;
        if (line.size() == 3 || line[3] == ' ') return true;
    }
    return false;
}

astra_mail::net::Socket SmtpClient::socketConnect(std::string& error) {
    return astra_mail::net::connectSocket(host_, port_, error);
}

SmtpClient::DeliveryResult SmtpClient::deliver(Session& ses, const std::string& mimeFile,
                                               const std::vector<std::string>& recipients) {
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(mimeFile.c_str(), "rb"), &std::fclose);
    return deliver(ses, file.get(), recipients);
}

SmtpClient::DeliveryResult SmtpClient::deliver(Session& ses, FILE* mime,
                                               const std::vector<std::string>& recipients) {
    DeliveryResult result;
    result.pending = recipients;
    if (ses.sock < 0 || recipients.empty() || !validAddress(user_)) {
        result.detail = "SMTP session, sender or recipients are not valid"; return result;
    }
    for (const auto& address : recipients)
        if (!validAddress(address)) { result.detail = "Invalid recipient address"; return result; }
    if (!mime || ::fseeko(mime, 0, SEEK_SET) != 0) { result.detail = "Cannot read MIME stream"; return result; }
    std::clearerr(mime);
    int code = 0;
    std::string reply;
    auto command = [&](const std::string& line, unsigned timeoutMs = 0) {
        reply.clear();
        if (!sendLine(ses.sock, line) || !recvResponse(ses, code, reply, timeoutMs)) {
            result.transportFailed = true;
            result.detail = "SMTP: " + (astra_mail::net::lastError().empty() ? std::string("connection/protocol error") : astra_mail::net::lastError());
            abortSession(ses); return false;
        }
        return true;
    };
    if (!command("MAIL FROM:<" + user_ + ">")) return result;
    if (code / 100 != 2) { result.detail = "MAIL FROM: " + reply; abortSession(ses); return result; }
    std::vector<std::string> accepted, rejected;
    std::string rejections;
    for (const auto& address : recipients) {
        if (!command("RCPT TO:<" + address + ">")) return result;
        if (code / 100 == 2) accepted.push_back(address);
        else if (code >= 400 && code <= 599 && code != 421) {
            rejected.push_back(address);
            if (!rejections.empty()) rejections += "; ";
            rejections += address + ": " + reply;
        } else { result.detail = "RCPT TO: " + reply; abortSession(ses); return result; }
    }
    if (accepted.empty()) {
        result.detail = rejections;
        abortSession(ses); return result;
    }
    if (!command("DATA", astra_mail::net::settingsFor(ses.sock).smtpDataStartMs)) return result;
    if (code != 354) { result.detail = "DATA: " + reply; abortSession(ses); return result; }
    if (!sendBody(ses.sock, mime)) {
        result.transportFailed = !astra_mail::net::lastError().empty();
        result.detail = "MIME read/send failed before DATA terminator: " + astra_mail::net::lastError();
        abortSession(ses); return result;
    }
    // From the first terminator byte onward a disconnect may hide an accepted message.
    result.pending = rejected;
    result.unknown = accepted;
    if (!command(".", astra_mail::net::settingsFor(ses.sock).smtpFinalMs)) {
        result.detail = "DATA result unknown: " + astra_mail::net::lastError() + "; " + rejections;
        return result;
    }
    if (code / 100 == 2) {
        result.delivered = accepted; result.unknown.clear();
        result.detail = rejections.empty() ? "SMTP accepted message" : rejections;
    } else if (code >= 400 && code <= 599) {
        result.pending = recipients; result.unknown.clear();
        result.detail = "DATA rejected: " + reply;
        abortSession(ses);
    } else {
        result.detail = "DATA result unknown: unexpected reply " + reply;
        abortSession(ses);
    }
    return result;
}

bool SmtpClient::sendMailFromFile(Session& ses, const std::string& file,
                                 const std::vector<std::string>& recipients, std::string& error) {
    const auto result = deliver(ses, file, recipients);
    error = result.detail;
    return result.complete();
}
bool SmtpClient::sendMailFromFile(Session& ses, const std::string& file,
                                 const std::string& recipient, std::string& error) {
    return sendMailFromFile(ses, file, std::vector<std::string>{recipient}, error);
}
bool SmtpClient::sendMailFromFile(const std::string& file,
                                 const std::vector<std::string>& recipients, std::string& error) {
    Session ses;
    if (!openSession(ses, error)) return false;
    const bool ok = sendMailFromFile(ses, file, recipients, error);
    closeSession(ses); return ok;
}
bool SmtpClient::sendMailFromFile(const std::string& file,
                                 const std::string& recipient, std::string& error) {
    return sendMailFromFile(file, std::vector<std::string>{recipient}, error);
}

std::string SmtpClient::base64(const std::string& input) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    unsigned value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) | c; bits += 8;
        while (bits >= 0) { output += table[(value >> bits) & 63]; bits -= 6; }
    }
    if (bits > -6) output += table[((value << 8) >> (bits + 8)) & 63];
    while (output.size() % 4) output += '=';
    return output;
}
