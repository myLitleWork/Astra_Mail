param([string]$Compiler = 'g++')

# Audit reproductions, not application tests or a cryptographic verification.
# Real production .cpp files are compiled unchanged. All sockets and libsodium
# calls are replaced with deterministic fakes. Only synthetic files are used.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$probeRoot = Join-Path $projectRoot 'build-audit-2026-11-10'
$shimRoot = Join-Path $probeRoot 'shims'
[void][IO.Directory]::CreateDirectory((Join-Path $shimRoot 'sys'))
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Write-ProbeFile([string]$RelativePath, [string]$Content) {
    [IO.File]::WriteAllText((Join-Path $probeRoot $RelativePath), $Content, $utf8)
}

Write-ProbeFile 'shims/netdb.h' @'
#pragma once
#include <cstddef>
#include <sys/types.h>
struct sockaddr {};
struct addrinfo {
    int ai_family = 0, ai_socktype = 1, ai_protocol = 0;
    sockaddr* ai_addr = nullptr;
    unsigned ai_addrlen = 0;
};
int getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);
void freeaddrinfo(addrinfo*);
'@
Write-ProbeFile 'shims/sys/socket.h' @'
#pragma once
#include <netdb.h>
using socklen_t = unsigned;
constexpr int SOCK_STREAM = 1, SOL_SOCKET = 1, SO_ERROR = 4;
int socket(int, int, int);
int connect(int, const sockaddr*, unsigned);
ssize_t send(int, const void*, size_t, int);
ssize_t recv(int, void*, size_t, int);
int getsockopt(int, int, int, void*, socklen_t*);
'@
Write-ProbeFile 'shims/unistd.h' @'
#pragma once
extern "C" int close(int);
'@
Write-ProbeFile 'shims/fcntl.h' @'
#pragma once
constexpr int F_GETFL = 1, F_SETFL = 2, O_NONBLOCK = 4;
inline int fcntl(int, int, int) { return 0; }
'@
Write-ProbeFile 'shims/sys/select.h' @'
#pragma once
struct audit_fd_set {};
struct audit_timeval { long tv_sec, tv_usec; };
#define fd_set audit_fd_set
#define timeval audit_timeval
#define FD_ZERO(p) ((void)0)
#define FD_SET(s,p) ((void)0)
inline int select(int, audit_fd_set*, audit_fd_set*, audit_fd_set*, audit_timeval*) { return 0; }
'@
Write-ProbeFile 'shims/sodium.h' @'
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
constexpr size_t crypto_pwhash_SALTBYTES = 16, crypto_secretbox_KEYBYTES = 32;
constexpr size_t crypto_secretbox_NONCEBYTES = 24, crypto_secretbox_MACBYTES = 16;
constexpr uint32_t crypto_pwhash_OPSLIMIT_MODERATE = 3;
constexpr uint32_t crypto_pwhash_MEMLIMIT_MODERATE = 268435456;
constexpr int crypto_pwhash_ALG_DEFAULT = 2;
inline uint64_t observed_ops = 0, observed_mem = 0;
inline bool decrypt_called = false;
inline int sodium_init() { return 0; }
inline void sodium_memzero(void* p, size_t n) { std::memset(p, 0, n); }
inline void randombytes_buf(void* p, size_t n) { std::memset(p, 1, n); }
inline int crypto_pwhash(unsigned char* p, size_t n, const char*, unsigned long long,
    const unsigned char*, unsigned long long ops, size_t mem, int) {
    observed_ops = ops; observed_mem = mem;
    std::memset(p, 0, n); return 0;
}
inline int crypto_secretbox_easy(unsigned char* out, const unsigned char*,
    unsigned long long n, const unsigned char*, const unsigned char*) {
    std::memset(out, 0, n + crypto_secretbox_MACBYTES); return 0;
}
inline int crypto_secretbox_open_easy(unsigned char*, const unsigned char*,
    unsigned long long, const unsigned char*, const unsigned char*) {
    decrypt_called = true; return -1;
}
#ifdef _WIN32
// Platform adaptation for the Linux-only backend; not a permissions test.
inline int chmod(const wchar_t*, int) { return 0; }
#endif
'@

Write-ProbeFile 'probe.cpp' @'
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/socket.h>

static std::deque<std::string> replies;
static std::vector<std::string> sent;
static std::string connected_host;
static int close_count = 0;
static bool fail_send = false;
int getaddrinfo(const char* host, const char*, const addrinfo*, addrinfo** out) {
    static addrinfo a;
    *out = &a; connected_host = host; return 0;
}
void freeaddrinfo(addrinfo*) {}
int socket(int, int, int) { return 10; }
int connect(int, const sockaddr*, unsigned) { return 0; }
extern "C" int close(int) { ++close_count; return 0; }
int getsockopt(int, int, int, void* out, socklen_t*) {
    *static_cast<int*>(out) = 0; return 0;
}
ssize_t send(int, const void* data, size_t n, int) {
    sent.emplace_back(static_cast<const char*>(data), n);
    return fail_send ? -1 : static_cast<ssize_t>(n);
}
ssize_t recv(int, void* out, size_t n, int) {
    if (replies.empty()) return 0;
    auto& r = replies.front();
    const size_t count = std::min(n, r.size());
    std::memcpy(out, r.data(), count);
    r.erase(0, count);
    if (r.empty()) replies.pop_front();
    return static_cast<ssize_t>(count);
}

#include "src/Utils/SmtpClient.cpp"
#include "src/Utils/ImapClient.cpp"
#include "src/core/thunderbird/ThunderbirdSettings.cpp"
#include "secrets/SecretBackendSodium.cpp"

static int checks = 0;
static void observed(bool condition, const char* name) {
    if (!condition) throw std::runtime_error(std::string("Not reproduced: ") + name);
    ++checks;
    std::cout << "OBSERVED " << name << '\n';
}
static void reset_transport(std::initializer_list<std::string> rs) {
    replies = rs; sent.clear(); close_count = 0; fail_send = false;
}
static void write_secret(const std::filesystem::path& path, uint32_t ops,
                         uint32_t mem, uint32_t declared, size_t actual) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    uint32_t version = 1;
    char salt[16]{}, nonce[24]{};
    out.write(reinterpret_cast<char*>(&version), 4);
    out.write(reinterpret_cast<char*>(&ops), 4);
    out.write(reinterpret_cast<char*>(&mem), 4);
    out.write(salt, 16); out.write(nonce, 24);
    out.write(reinterpret_cast<char*>(&declared), 4);
    const std::string bytes(actual, 'x');
    out.write(bytes.data(), bytes.size());
    if (!out) throw std::runtime_error("Cannot write synthetic secret");
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected synthetic data directory");
        const std::filesystem::path data = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(data / ".astramail");
        const auto mime = (data / "synthetic.mime").string();
        { std::ofstream f(mime); f << "Subject: synthetic\r\n\r\nprobe\r\n"; }

        SmtpClient smtp("smtp-a.invalid", 25);
        smtp.setCredentials("a@example.invalid", "synthetic-pass");
        std::string error;
        SmtpClient::Session session;
        reset_transport({"220 ready\r\n", "250 hello\r\n", "334 user\r\n",
                         "334 pass\r\n", "235 accepted\r\n"});
        const bool control = smtp.openSession(session, error);
        observed(control && session.sock == 10, "SMTP positive control");
        SmtpClient::closeSession(session);
        observed(session.sock == -1 && close_count == 1, "explicit close invalidates descriptor");

        reset_transport({"220 ready\r\n", "250 hello\r\n", "334 user\r\n",
                         "334 pass\r\n", "535 authentication failed\r\n"});
        observed(smtp.openSession(session, error), "SMTP 535 still reports authenticated session");
        SmtpClient::closeSession(session);

        reset_transport({});
        observed(smtp.openSession(session, error), "SMTP EOF during handshake still reports success");
        SmtpClient::closeSession(session);

        session.sock = 10;
        reset_transport({"250 sender\r\n", "550 rejected recipient\r\n",
                         "503 invalid state\r\n", "554 transaction failed\r\n"});
        observed(smtp.sendMailFromFile(session, mime, "b@example.invalid", error),
                 "SMTP recipient and DATA rejection still reports sent");

        reset_transport({"250 sender\r\n", "250 recipient\r\n",
                         "354 proceed\r\n", "554 rejected message\r\n"});
        observed(smtp.sendMailFromFile(session, mime, "b@example.invalid", error),
                 "SMTP final 554 still reports sent");

        reset_transport({"250 sender\r\n", "250 recipient\r\n", "354 proceed\r\n"});
        observed(smtp.sendMailFromFile(session, mime, "b@example.invalid", error),
                 "SMTP disconnect before final acknowledgement still reports sent");

        reset_transport({});
        { SmtpClient::Session leaked; leaked.sock = 10; }
        { ImapSession leaked; leaked.sock = 10; }
        observed(close_count == 0, "SMTP and IMAP scope exit do not close sessions");

        reset_transport({"A1 BAD syntax\r\n"});
        ImapSession imap; imap.sock = 10;
        observed(!imapLogin(imap, "a@example.invalid", "two words", error) &&
                 sent.at(0) == "A1 LOGIN a@example.invalid two words\r\n",
                 "IMAP password containing space sent without quoting");

        reset_transport({"A1 BAD syntax\r\n"});
        imap.nextTag = 1;
        imapLogin(imap, "a@example.invalid", "x\r\nA9 NOOP", error);
        observed(sent.at(0).find("\r\nA9 NOOP\r\n") != std::string::npos,
                 "IMAP credential CRLF reaches command stream");

        auto& tb = ThunderbirdSettings::instance();
        astra_mail::TbProfile profile; profile.id = "synthetic-a";
        astra_mail::LocalMailSettings settings;
        settings.accountId = "account-a"; settings.userName = "a@example.invalid";
        settings.imapHost = "imap-a.invalid"; settings.smtpHost = "smtp-a.invalid";
        tb.loadFromLocalCache(profile, settings);
        tb.setMailPassword("synthetic-pass-a");
        profile.id = "synthetic-b"; settings.accountId = "account-b";
        settings.userName = "b@example.invalid"; settings.imapHost = "imap-b.invalid";
        settings.smtpHost = "smtp-b.invalid";
        tb.loadFromLocalCache(profile, settings);
        const auto& active_smtp = tb.smtpServers().at(tb.active().smtpServerId);
        SmtpClient switched(active_smtp.host, active_smtp.port);
        switched.setCredentials(active_smtp.user, tb.mailPassword());
        reset_transport({"220 ready\r\n", "250 hello\r\n", "334 user\r\n",
                         "334 pass\r\n", "535 rejected\r\n"});
        switched.openSession(session, error);
        observed(tb.mailPassword() == "synthetic-pass-a" &&
                 connected_host == "smtp-b.invalid" &&
                 sent.at(3) == SmtpClient::base64("synthetic-pass-a") + "\r\n",
                 "switch to account B sends account A password to server B");
        SmtpClient::closeSession(session);

        // Set HOME only inside this disposable child process. Never read the
        // real user's secret, profile cache, mail, database, or configuration.
        _putenv_s("HOME", data.string().c_str());
        SecretBackendSodium backend;
        const auto secret = data / ".astramail" / "secret.bin";
        // The runner supplies a new directory for each invocation.
        observed(!std::filesystem::exists(secret) && backend.isInitialized(),
                 "secret initialization means crypto ready, not file exists");

        std::string password;
        write_secret(secret, 3, 268435456, 0, 0);
        bool length_error = false;
        try { backend.load("synthetic-pin", password); }
        catch (const std::length_error&) { length_error = true; }
        observed(length_error, "secret ciphertext shorter than MAC throws length_error");

        write_secret(secret, UINT32_MAX, UINT32_MAX, 16, 16);
        backend.load("synthetic-pin", password);
        observed(observed_ops == UINT32_MAX && observed_mem == UINT32_MAX,
                 "secret unbounded KDF parameters passed to crypto boundary");

        write_secret(secret, 3, 268435456, 32, 16);
        decrypt_called = false;
        backend.load("synthetic-pin", password);
        observed(decrypt_called, "truncated ciphertext reaches decryption instead of read error");

        std::cout << "REPRODUCTIONS " << checks << "/" << checks << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PROBE FAILED: " << e.what() << '\n';
        return 1;
    }
}
'@

$sourceFile = Join-Path $probeRoot 'probe.cpp'
$probeExe = Join-Path $probeRoot 'probe.exe'
& $Compiler '-std=c++17' '-O0' "-I$shimRoot" "-I$projectRoot" "-I$(Join-Path $projectRoot 'src')" $sourceFile '-o' $probeExe '-static-libgcc' '-static-libstdc++'
if ($LASTEXITCODE -ne 0) { throw "Audit probe compilation failed: $LASTEXITCODE" }
$dataRoot = Join-Path $probeRoot ('synthetic-' + [Guid]::NewGuid().ToString('N'))
& $probeExe $dataRoot
if ($LASTEXITCODE -ne 0) { throw "Audit probe execution failed: $LASTEXITCODE" }
