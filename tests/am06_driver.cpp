// Real secret backend/commands; fixtures contain synthetic data only.
#include "Utils/SecretInput.h"
#include "menu/Commands/StartUpMenu/CmdEnterPin.h"
#include "menu/Commands/StartUpMenu/CmdResetPin.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/LocalProfileCache.h"
#include "secrets/SecretManager.h"
#include <sodium.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <signal.h>
static const std::string oldPin = "  synthetic legacy pin with spaces  ";
static const std::string newPin = "synthetic-new-pin";
static const std::string password = "  synthetic mail password with spaces  ";

static void seedLegacy() {
    if (sodium_init() < 0) throw std::runtime_error("sodium init");
    unsigned char salt[crypto_pwhash_SALTBYTES], nonce[crypto_secretbox_NONCEBYTES], key[crypto_secretbox_KEYBYTES];
    randombytes_buf(salt,sizeof(salt)); randombytes_buf(nonce,sizeof(nonce));
    if (crypto_pwhash(key,sizeof(key),oldPin.data(),oldPin.size(),salt,crypto_pwhash_OPSLIMIT_MODERATE,
                     crypto_pwhash_MEMLIMIT_MODERATE,crypto_pwhash_ALG_DEFAULT) != 0) throw std::runtime_error("KDF");
    std::vector<unsigned char> cipher(password.size()+crypto_secretbox_MACBYTES);
    crypto_secretbox_easy(cipher.data(),reinterpret_cast<const unsigned char*>(password.data()),password.size(),nonce,key);
    sodium_memzero(key,sizeof(key));
    const fs::path root = fs::path(std::getenv("HOME")) / ".astramail";
    fs::create_directories(root);
    std::ofstream out((root/"secret.bin").string(),std::ios::binary);
    uint32_t version=1, ops=crypto_pwhash_OPSLIMIT_MODERATE, mem=crypto_pwhash_MEMLIMIT_MODERATE, length=cipher.size();
    out.write(reinterpret_cast<char*>(&version),4); out.write(reinterpret_cast<char*>(&ops),4);
    out.write(reinterpret_cast<char*>(&mem),4); out.write(reinterpret_cast<char*>(salt),sizeof(salt));
    out.write(reinterpret_cast<char*>(nonce),sizeof(nonce)); out.write(reinterpret_cast<char*>(&length),4);
    out.write(reinterpret_cast<char*>(cipher.data()),cipher.size());
    if (!out) throw std::runtime_error("legacy write");
}
int main(int argc,char** argv) {
    try {
        if(argc<2) return 2;
        const std::string mode=argv[1];
        if(mode=="read" || mode=="short" || mode=="throw" || mode=="thread") {
            std::string value="stale-value";
            if(mode=="throw") std::cin.exceptions(std::ios::failbit|std::ios::badbit);
            std::thread worker;
            if(mode=="thread") worker=std::thread([](){std::this_thread::sleep_for(std::chrono::milliseconds(500)); ::raise(SIGTERM);});
            bool ok=false, exception=false;
            try { ok=astra_mail::readSecret("SECRET> ",value,mode=="short"?8:128); }
            catch(const std::ios_base::failure&) { exception=true; }
            std::cout << "READ_OK=" << ok << " CLEARED=" << value.empty() << " SIZE=" << value.size()
                      << " EXACT=" << (value=="  synthetic value \t tail  ") << " EXCEPTION=" << exception << "\n" << std::flush;
            if(worker.joinable()) worker.join();
            return 0;
        }
        astra_mail::TbProfile profile; profile.id="synthetic-am06";
        astra_mail::LocalMailSettings settings;
        settings.profileId=profile.id; settings.accountId="synthetic";
        settings.email=settings.userName="sender@example.invalid";
        settings.smtpHost=settings.imapHost="127.0.0.1";
        settings.smtpPort=2525; settings.imapPort=1143;
        auto& tb=ThunderbirdSettings::instance();
        if(!tb.loadFromLocalCache(profile,settings)) throw std::runtime_error("profile");
        auto& secrets=SecretManager::instance();
        if(mode=="seed-old" || mode=="seed-new" || mode=="seed-legacy") {
            bool ok=true;
            if(mode=="seed-legacy") seedLegacy();
            else ok=secrets.store(tb.secretContext(),password,mode=="seed-old"?oldPin:newPin);
            if(!astra_mail::saveLocalMailSettings(settings) || !astra_mail::saveLocalContacts({},{})) ok=false;
            std::cout << "SEEDED=" << ok << "\n";
        } else if(mode=="enter" || mode=="reset") {
            tb.setMailPassword("stale-password");
            if(mode=="enter") { astra_mail::CmdEnterPin command; command.execute(); }
            else { astra_mail::CmdResetPin command; command.execute(); }
            std::cout << "UNLOCKED=" << tb.hasMailPassword() << " EXACT=" << (tb.mailPassword()==password) << "\n";
        } else if(mode=="check-new" || mode=="check-old") {
            std::string value;
            bool ok=secrets.load(tb.secretContext(),mode=="check-old"?oldPin:newPin,value);
            std::cout << "LOADED=" << ok << " EXACT=" << (value==password) << "\n";
        } else if(mode=="policy") {
            std::cout << "PIN> " << std::flush;
            std::string value; std::getline(std::cin,value);
            std::cout << "VALID=" << astra_mail::validNewPin(value) << "\n";
        } else return 2;
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
