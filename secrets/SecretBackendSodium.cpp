#include "SecretBackendSodium.h"
#include <sodium.h>


#if __has_include(<filesystem>)
#include <filesystem>
  namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif



#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t maxContext = 16384, maxPassword = 4096, maxFile = 65536;
struct Sensitive {
    std::vector<unsigned char> bytes;
    explicit Sensitive(size_t n = 0) : bytes(n) {}
    ~Sensitive() { if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size()); }
};
void wipe(std::string& value) {
    if (!value.empty()) sodium_memzero(&value[0], value.size());
    value.clear();
}
fs::path rootPath() {
    const char* home = std::getenv("HOME");
    return home && *home ? fs::path(home) / ".astramail" : fs::path{};
}
fs::path accountDir() {
    const auto root = rootPath();
    return root.empty() ? fs::path{} : root / "accounts";
}
bool directoryReady(bool create) {
    const auto dir = accountDir();
    if (dir.empty()) return false;
    if (create) { std::error_code ec; fs::create_directories(dir, ec); if (ec) return false; }
    struct stat st{};
    if (::lstat(dir.string().c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) return false;
    return !create || ::chmod(dir.string().c_str(), 0700) == 0;
}
std::string contextId(const std::string& context) {
    if (context.empty() || context.size() > maxContext) return {};
    unsigned char digest[32];
    if (crypto_generichash(digest, sizeof(digest), reinterpret_cast<const unsigned char*>(context.data()),
                          context.size(), nullptr, 0) != 0) return {};
    static const char hex[] = "0123456789abcdef";
    std::string id;
    for (unsigned char c : digest) { id += hex[c >> 4]; id += hex[c & 15]; }
    return id;
}
bool pathExists(const fs::path& path) {
    struct stat st{};
    return ::lstat(path.string().c_str(), &st) == 0;
}
bool readFile(const fs::path& path, std::vector<unsigned char>& data) {
    data.clear();
    const int fd = ::open(path.string().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st{};
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == ::geteuid() &&
              st.st_size >= 0 && static_cast<uint64_t>(st.st_size) <= maxFile;
    unsigned char buffer[4096];
    while (ok) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = false; break; }
        if (!n) break;
        data.insert(data.end(), buffer, buffer + n);
        if (data.size() > maxFile) ok = false;
    }
    ::close(fd);
    return ok;
}
bool atomicWrite(const fs::path& destination, const std::vector<unsigned char>& data) {
    std::string pattern = destination.string() + ".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    const int fd = ::mkstemp(name.data());
    if (fd < 0) return false;
    size_t done = 0;
    bool ok = true;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        done += static_cast<size_t>(n);
    }
    if (ok) ok = ::fsync(fd) == 0;
    if (::close(fd) != 0) ok = false;
    if (ok) ok = ::rename(name.data(), destination.string().c_str()) == 0;
    if (!ok) { ::unlink(name.data()); return false; }
    const int dir = ::open(destination.parent_path().string().c_str(), O_RDONLY | O_DIRECTORY);
    ok = dir >= 0 && ::fsync(dir) == 0;
    if (dir >= 0) ::close(dir);
    return ok;
}
struct StoreLock {
    int fd = -1;
    StoreLock() {
        if (!directoryReady(true)) return;
        fd = ::open((accountDir() / ".lock").string().c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_NONBLOCK, 0600);
        struct stat st{};
        if (fd >= 0 && (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || ::flock(fd, LOCK_EX) != 0)) {
            ::close(fd); fd = -1;
        }
    }
    ~StoreLock() { if (fd >= 0) ::close(fd); }
};
void put32(std::vector<unsigned char>& data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.push_back(static_cast<unsigned char>(value >> (i * 8)));
}
bool get32(const std::vector<unsigned char>& data, size_t& offset, uint32_t& value) {
    if (offset > data.size() || data.size() - offset < 4) return false;
    value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[offset++]) << (i * 8);
    return true;
}
bool derive(Sensitive& key, const std::string& pin, const unsigned char* salt,
            uint32_t ops, uint32_t mem, uint32_t algorithm) {
    // The old MODERATE profiles and the new format are bounded before KDF/allocation.
    if (pin.empty() || pin.size() > 1024 || ops < 1 || ops > 10 || mem < 8u * 1024u ||
        mem > 512u * 1024u * 1024u ||
        (algorithm != crypto_pwhash_ALG_ARGON2I13 && algorithm != crypto_pwhash_ALG_DEFAULT)) return false;
    return crypto_pwhash(key.bytes.data(), key.bytes.size(), pin.data(), pin.size(), salt,
                         ops, mem, algorithm) == 0;
}
bool storeBound(const std::string& context, const std::string& password, const std::string& pin) {
    const auto id = contextId(context);
    if (id.empty() || password.empty() || password.size() > maxPassword || pin.empty() || pin.size() > 1024) return false;
    Sensitive plain;
    put32(plain.bytes, static_cast<uint32_t>(context.size()));
    plain.bytes.insert(plain.bytes.end(), context.begin(), context.end());
    plain.bytes.insert(plain.bytes.end(), password.begin(), password.end());
    unsigned char salt[crypto_pwhash_SALTBYTES], nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(salt, sizeof(salt)); randombytes_buf(nonce, sizeof(nonce));
    Sensitive key(crypto_secretbox_KEYBYTES);
    if (!derive(key, pin, salt, crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                crypto_pwhash_ALG_DEFAULT)) return false;
    std::vector<unsigned char> cipher(plain.bytes.size() + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(cipher.data(), plain.bytes.data(), plain.bytes.size(), nonce, key.bytes.data()) != 0) return false;
    std::vector<unsigned char> file;
    put32(file, 2); put32(file, crypto_pwhash_ALG_DEFAULT);
    put32(file, crypto_pwhash_OPSLIMIT_MODERATE); put32(file, crypto_pwhash_MEMLIMIT_MODERATE);
    put32(file, static_cast<uint32_t>(cipher.size()));
    file.insert(file.end(), salt, salt + sizeof(salt)); file.insert(file.end(), nonce, nonce + sizeof(nonce));
    file.insert(file.end(), cipher.begin(), cipher.end());
    return atomicWrite(accountDir() / (id + ".bin"), file);
}
bool decryptFile(const std::vector<unsigned char>& file, const std::string& pin,
                 bool legacy, Sensitive& plain) {
    size_t offset = 0;
    uint32_t version = 0, algorithm = crypto_pwhash_ALG_DEFAULT, ops = 0, mem = 0, length = 0;
    if (!get32(file, offset, version) || version != (legacy ? 1u : 2u)) return false;
    if (!legacy && !get32(file, offset, algorithm)) return false;
    if (!get32(file, offset, ops) || !get32(file, offset, mem)) return false;
    if (!legacy && !get32(file, offset, length)) return false;
    if (file.size() - offset < crypto_pwhash_SALTBYTES + crypto_secretbox_NONCEBYTES) return false;
    const auto salt = file.data() + offset; offset += crypto_pwhash_SALTBYTES;
    const auto nonce = file.data() + offset; offset += crypto_secretbox_NONCEBYTES;
    if (legacy && !get32(file, offset, length)) return false;
    if (length < crypto_secretbox_MACBYTES || length != file.size() - offset ||
        length > (legacy ? maxPassword : maxContext + maxPassword + 4) + crypto_secretbox_MACBYTES) return false;
    Sensitive key(crypto_secretbox_KEYBYTES);
    if (!derive(key, pin, salt, ops, mem, algorithm)) return false;
    plain.bytes.resize(length - crypto_secretbox_MACBYTES);
    return crypto_secretbox_open_easy(plain.bytes.data(), file.data() + offset, length, nonce, key.bytes.data()) == 0;
}
bool legacyAllowed(const std::string& context) {
    const auto root = rootPath();
    const auto id = contextId(context);
    if (root.empty() || id.empty() || !pathExists(root / "secret.bin")) return false;
    const auto marker = accountDir() / "legacy-binding";
    if (!pathExists(marker)) return true;
    std::vector<unsigned char> contents;
    return readFile(marker, contents) && std::string(contents.begin(), contents.end()) == id;
}
}

SecretBackendSodium::SecretBackendSodium() { initialized_ = sodium_init() >= 0; }
bool SecretBackendSodium::isInitialized() const { return initialized_; }

bool SecretBackendSodium::hasSecret(const std::string& context) const {
    try {
        const auto id = contextId(context);
        return initialized_ && !id.empty() && directoryReady(false) && pathExists(accountDir() / (id + ".bin"));
    } catch (...) { return false; }
}
bool SecretBackendSodium::legacyAvailable(const std::string& context) const {
    try { return initialized_ && !hasSecret(context) && legacyAllowed(context); }
    catch (...) { return false; }
}
bool SecretBackendSodium::store(const std::string& context, const std::string& password, const std::string& pin) {
    try { StoreLock lock; return initialized_ && lock.fd >= 0 && storeBound(context, password, pin); }
    catch (...) { return false; }
}
bool SecretBackendSodium::load(const std::string& context, const std::string& pin, std::string& password) {
    wipe(password);
    try {
        const auto id = contextId(context);
        std::vector<unsigned char> file;
        Sensitive plain;
        if (!initialized_ || id.empty() || !directoryReady(false) ||
            !readFile(accountDir() / (id + ".bin"), file) || !decryptFile(file, pin, false, plain)) return false;
        size_t offset = 0; uint32_t length = 0;
        if (!get32(plain.bytes, offset, length) || length != context.size() || length > plain.bytes.size() - offset ||
            std::memcmp(plain.bytes.data() + offset, context.data(), length) != 0) return false;
        offset += length;
        if (plain.bytes.size() <= offset || plain.bytes.size() - offset > maxPassword) return false;
        password.assign(reinterpret_cast<const char*>(plain.bytes.data() + offset), plain.bytes.size() - offset);
        return true;
    } catch (...) { wipe(password); return false; }
}
bool SecretBackendSodium::migrateLegacy(const std::string& context, const std::string& pin) {
    try {
        StoreLock lock;
        if (!initialized_ || lock.fd < 0 || hasSecret(context) || !legacyAllowed(context)) return false;
        std::vector<unsigned char> file;
        Sensitive plain;
        if (!readFile(rootPath() / "secret.bin", file) || !decryptFile(file, pin, true, plain) || plain.bytes.empty()) return false;
        const auto id = contextId(context);
        const auto marker = accountDir() / "legacy-binding";
        // Claim the old unbound file BEFORE creating the new bound secret. If the
        // second step fails, only this same account can resume the migration.
        if (!pathExists(marker) && !atomicWrite(marker, std::vector<unsigned char>(id.begin(), id.end()))) return false;
        std::string password(reinterpret_cast<const char*>(plain.bytes.data()), plain.bytes.size());
        bool ok = false;
        try { ok = storeBound(context, password, pin); } catch (...) { wipe(password); throw; }
        wipe(password);
        return ok;
    } catch (...) { return false; }
}
