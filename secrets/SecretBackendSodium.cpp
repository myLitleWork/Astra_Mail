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


SecretBackendSodium::SecretBackendSodium()
{
    initialized_ = (sodium_init() >= 0);
}

bool SecretBackendSodium::isInitialized() const
{
    return initialized_;
}

static fs::path secretFilePath()
{
    const char* home = std::getenv("HOME");
    if (!home) return {};

    auto dir = fs::path(home) / ".astramail";
    fs::create_directories(dir);

    return dir / "secret.bin";
}


bool SecretBackendSodium::store(const std::string& password,
                                const std::string& pin)
{
    if (!initialized_) return false;

    std::vector<unsigned char> salt(crypto_pwhash_SALTBYTES);
    randombytes_buf(salt.data(), salt.size());

    std::vector<unsigned char> key(crypto_secretbox_KEYBYTES);

    if (crypto_pwhash(
            key.data(), key.size(),
            pin.data(), pin.size(),
            salt.data(),
            crypto_pwhash_OPSLIMIT_MODERATE,
            crypto_pwhash_MEMLIMIT_MODERATE,
            crypto_pwhash_ALG_DEFAULT) != 0)
    {
        sodium_memzero(key.data(), key.size());
        return false;
    }

    std::vector<unsigned char> nonce(crypto_secretbox_NONCEBYTES);
    randombytes_buf(nonce.data(), nonce.size());

    std::vector<unsigned char> cipher(
            password.size() + crypto_secretbox_MACBYTES
    );

    crypto_secretbox_easy(
            cipher.data(),
            reinterpret_cast<const unsigned char*>(password.data()),
            password.size(),
            nonce.data(),
            key.data()
    );

    auto path = secretFilePath();
    if (path.empty()) return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    uint32_t version = 1;
    uint32_t ops = crypto_pwhash_OPSLIMIT_MODERATE;
    uint32_t mem = crypto_pwhash_MEMLIMIT_MODERATE;
    uint32_t clen = cipher.size();

    out.write((char*)&version, sizeof(version));
    out.write((char*)&ops, sizeof(ops));
    out.write((char*)&mem, sizeof(mem));
    out.write((char*)salt.data(), salt.size());
    out.write((char*)nonce.data(), nonce.size());
    out.write((char*)&clen, sizeof(clen));
    out.write((char*)cipher.data(), cipher.size());

    ::chmod(path.c_str(), 0600);

    sodium_memzero(key.data(), key.size());

    return true;
}

bool SecretBackendSodium::load(const std::string& pin,
                               std::string& outPassword)
{
    if (!initialized_) return false;

    auto path = secretFilePath();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t version, ops, mem, clen;
    in.read((char*)&version, sizeof(version));
    if (version != 1) return false;

    in.read((char*)&ops, sizeof(ops));
    in.read((char*)&mem, sizeof(mem));

    std::vector<unsigned char> salt(crypto_pwhash_SALTBYTES);
    std::vector<unsigned char> nonce(crypto_secretbox_NONCEBYTES);

    in.read((char*)salt.data(), salt.size());
    in.read((char*)nonce.data(), nonce.size());
    in.read((char*)&clen, sizeof(clen));

    std::vector<unsigned char> cipher(clen);
    in.read((char*)cipher.data(), cipher.size());

    std::vector<unsigned char> key(crypto_secretbox_KEYBYTES);

    if (crypto_pwhash(
            key.data(), key.size(),
            pin.data(), pin.size(),
            salt.data(),
            ops,
            mem,
            crypto_pwhash_ALG_DEFAULT) != 0)
    {
        return false;
    }

    std::vector<unsigned char> plain(
            cipher.size() - crypto_secretbox_MACBYTES
    );

    if (crypto_secretbox_open_easy(
            plain.data(),
            cipher.data(),
            cipher.size(),
            nonce.data(),
            key.data()) != 0)
    {
        sodium_memzero(key.data(), key.size());
        return false; // неверный PIN
    }

    outPassword.assign(
            reinterpret_cast<char*>(plain.data()),
            plain.size()
    );

    sodium_memzero(key.data(), key.size());
    sodium_memzero(plain.data(), plain.size());

    return true;
}


