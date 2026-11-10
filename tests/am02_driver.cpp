// Native AM-02 entry points. Every secret and profile in this driver is synthetic.
#include "secrets/SecretManager.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/mail/AccountAccess.h"
#include "core/mail/MailDeliveryState.h"
#include "core/mail/MailSendPipeline.h"
#include "menu/Commands/StartUpMenu/CmdEnterPin.h"
#include "menu/Commands/StartUpMenu/CmdResetPin.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "menu/Commands/Mails/CmdExportUnreadToFolders.h"
#include "core/paths/AstraMailPaths.h"
#include "menu/StartupProfileLoader.h"
#include "menu/Commands/Server/CmdApplyThunderbirdAccount.h"
#include <sodium.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <thread>

namespace astra_mail { MailSendPipeline createPipelineFromThunderbird(); }
static const char* testPin = "synthetic-am02-pin";
static std::string expected(const std::string& variant) { return "synthetic-password-" + variant; }
static void require(bool value, const char* name) { if (!value) throw std::runtime_error(name); }

struct Account {
    astra_mail::TbProfile profile;
    astra_mail::LocalMailSettings settings;
    Account(const std::string& variant, int smtp, int imap) {
        profile.id = "synthetic-profile-" + variant;
        profile.path = "/synthetic/profile-" + variant;
        settings.profileId = profile.id; settings.profilePath = profile.path;
        settings.accountId = "synthetic-account-" + variant;
        settings.email = settings.userName = variant + "@example.invalid";
        settings.smtpHost = settings.imapHost = "127.0.0.1";
        settings.smtpPort = smtp; settings.imapPort = imap;
    }
    bool apply() const { return ThunderbirdSettings::instance().loadFromLocalCache(profile, settings); }
};

static void legacyFixture(const std::string& password) {
    require(sodium_init() >= 0, "sodium_init");
    const fs::path dir = fs::path(std::getenv("HOME")) / ".astramail";
    fs::create_directories(dir);
    unsigned char salt[crypto_pwhash_SALTBYTES], nonce[crypto_secretbox_NONCEBYTES], key[crypto_secretbox_KEYBYTES];
    randombytes_buf(salt, sizeof(salt)); randombytes_buf(nonce, sizeof(nonce));
    require(crypto_pwhash(key, sizeof(key), testPin, std::strlen(testPin), salt,
        crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE, crypto_pwhash_ALG_DEFAULT) == 0, "legacy KDF");
    std::vector<unsigned char> cipher(password.size() + crypto_secretbox_MACBYTES);
    require(crypto_secretbox_easy(cipher.data(), reinterpret_cast<const unsigned char*>(password.data()),
        password.size(), nonce, key) == 0, "legacy encryption");
    sodium_memzero(key, sizeof(key));
    std::ofstream file((dir / "secret.bin").string(), std::ios::binary);
    uint32_t version = 1, ops = crypto_pwhash_OPSLIMIT_MODERATE, mem = crypto_pwhash_MEMLIMIT_MODERATE, length = cipher.size();
    file.write(reinterpret_cast<char*>(&version), 4); file.write(reinterpret_cast<char*>(&ops), 4);
    file.write(reinterpret_cast<char*>(&mem), 4); file.write(reinterpret_cast<char*>(salt), sizeof(salt));
    file.write(reinterpret_cast<char*>(nonce), sizeof(nonce)); file.write(reinterpret_cast<char*>(&length), 4);
    file.write(reinterpret_cast<char*>(cipher.data()), cipher.size());
    require(static_cast<bool>(file), "legacy write");
}

int main(int argc, char** argv) {
    try {
        if (argc < 5) return 2;
        const std::string mode = argv[1], variant = argv[2];
        Account account(variant, std::stoi(argv[3]), std::stoi(argv[4]));
        auto& tb = ThunderbirdSettings::instance();
        auto& secrets = SecretManager::instance();
        require(account.apply(), "apply initial account");
        const auto context = tb.secretContext();
        const std::string pin = argc > 5 ? argv[5] : testPin;
        if (mode == "store") {
            std::cout << "STORED=" << secrets.store(context, expected(variant), pin) << "\n";
        } else if (mode == "load") {
            std::string password = "stale-output";
            const bool ok = secrets.load(context, pin, password);
            std::cout << "LOADED=" << ok << " MATCH=" << (ok && password == expected(variant))
                      << " CLEARED=" << (!ok && password.empty()) << "\n";
        } else if (mode == "has") {
            std::cout << "HAS=" << secrets.hasSecret(context) << " LEGACY=" << secrets.legacyAvailable(context) << "\n";
        } else if (mode == "pinmenu") {
            std::cout << "UNLOCKED=" << astra_mail::pinMenu() << "\n";
        } else if (mode == "blocked-apply") {
            require(tb.setMailPassword(expected(variant)), "unlock before blocked command");
            Account other("B", account.settings.smtpPort, account.settings.imapPort);
            astra_mail::MailOperationScope operation;
            AccountConfig config;
            config.accountId = other.settings.accountId;
            astra_mail::CmdApplyThunderbirdAccount command(config, other.profile);
            command.execute();
            require(tb.secretContext() == context && tb.hasMailPassword(), "command must preserve current account");
            std::cout << "APPLY_COMMAND_BLOCKED=1\n";
        } else if (mode == "legacy" || mode == "legacyseed") {
            legacyFixture(expected(variant)); std::cout << "LEGACY_CREATED=1\n";
            if (mode == "legacyseed") {
                require(astra_mail::saveLocalMailSettings(account.settings) &&
                        astra_mail::saveLocalContacts({}, {}), "seed legacy cache");
            }
        } else if (mode == "migrate") {
            std::cout << "MIGRATED=" << secrets.migrateLegacy(context, pin) << "\n";
        } else if (mode == "enter" || mode == "reset") {
            require(tb.setMailPassword("stale-memory"), "preload stale memory");
            if (mode == "enter") { astra_mail::CmdEnterPin command; command.execute(); }
            else { astra_mail::CmdResetPin command; command.execute(); }
            std::cout << "UNLOCKED=" << tb.hasMailPassword() << " MATCH=" << (tb.mailPassword() == expected(variant)) << "\n";
        } else if (mode == "switch") {
            Account other("B", account.settings.smtpPort, account.settings.imapPort);
            require(secrets.store(context, expected(variant), testPin), "store A");
            require(tb.setMailPassword(expected(variant)), "unlock A");
            require(other.apply() && !tb.hasMailPassword() && tb.mailPassword().empty(), "A to B clears password");
            const auto otherContext = tb.secretContext();
            require(context != otherContext, "different context");
            require(secrets.store(otherContext, expected("B"), testPin), "store B");
            std::string password;
            require(secrets.load(otherContext, testPin, password) && password == expected("B"), "unlock B");
            require(tb.setMailPassword(password), "set B");
            require(account.apply() && !tb.hasMailPassword(), "B to A clears password");
            require(secrets.load(tb.secretContext(), testPin, password) && password == expected(variant), "A secret retained");
            std::cout << "SWITCH_OK=1\n";
        } else if (mode == "contexts") {
            std::set<std::string> contexts{context};
            for (int i = 0; i < 9; ++i) {
                Account changed = account;
                if (i == 0) changed.profile.id += "-other";
                if (i == 1) changed.profile.path += "-other";
                if (i == 2) changed.settings.accountId += "-other";
                if (i == 3) changed.settings.imapHost = "imap.example.invalid";
                if (i == 4) ++changed.settings.imapPort;
                if (i == 5) changed.settings.smtpHost = "smtp.example.invalid";
                if (i == 6) ++changed.settings.smtpPort;
                if (i == 7) changed.settings.userName = "other@example.invalid";
                if (i == 8) changed.settings.email = "other@example.invalid";
                require(changed.apply() && contexts.insert(tb.secretContext()).second, "context field binding");
            }
            Account hosts = account;
            hosts.settings.smtpHost = "MAIL.EXAMPLE.INVALID"; require(hosts.apply(), "host context");
            const auto canonical = tb.secretContext();
            hosts.settings.smtpHost = "mail.example.invalid."; require(hosts.apply(), "host canonical");
            require(canonical == tb.secretContext(), "host normalization");
            std::cout << "CONTEXT_FIELDS_OK=9 CANONICAL_HOST=1\n";
        } else if (mode == "gate") {
            require(tb.setMailPassword(expected(variant)), "gate initial secret");
            Account other("B", account.settings.smtpPort, account.settings.imapPort);
            {
                astra_mail::MailOperationScope operation;
                require(!other.apply() && !tb.applyAccount(account.settings.accountId) &&
                        !tb.setCurrentProfile(other.profile) && !tb.load("/synthetic/no-prefs.js") &&
                        !tb.setMailPassword("wrong"), "same-thread mutation gate");
                require(tb.secretContext() == context && tb.hasMailPassword(), "gate preserves old context");
            }
            std::promise<void> ready, release;
            auto released = release.get_future();
            std::thread worker([&]() { astra_mail::MailOperationScope operation; ready.set_value(); released.wait(); });
            ready.get_future().wait();
            const bool denied = !other.apply();
            release.set_value(); worker.join();
            require(denied && other.apply() && !tb.hasMailPassword(), "cross-thread mutation gate");
            std::cout << "GATE_OK=1\n";
        } else if (mode == "locked-outbox" || mode == "stale" || mode == "inflight") {
            require(tb.setMailPassword(expected(variant)), "initial live secret");
            Account other("B", account.settings.smtpPort, account.settings.imapPort);
            if (mode == "locked-outbox") {
                require(other.apply(), "switch locked");
                require(!astra_mail::CmdSendMailsFromOutbox::runSendFromOutbox(&std::cout), "locked outbox must fail");
                std::cout << "LOCKED_SEND_BLOCKED=1\n";
            } else if (mode == "stale") {
                auto pipeline = astra_mail::createPipelineFromThunderbird();
                astra_mail::CmdExportUnreadToFolders exportCommand(account.settings.imapHost, account.settings.imapPort,
                    account.settings.userName, expected(variant), astra_mail::ImapFolder("INBOX", "INBOX"),
                    astra_mail::getInboxRoot().string(), astra_mail::ExportStrategy{astra_mail::ExportMode::BySubject, false});
                require(other.apply() && tb.setMailPassword(expected("B")), "switch/unlock B");
                astra_mail::MailFolderInfo info;
                std::string log;
                require(!pipeline.send(info, log), "stale pipeline blocked");
                exportCommand.execute();
                std::cout << "STALE_COMMANDS_BLOCKED=1\n";
            } else {
                std::thread worker([&]() { astra_mail::CmdSendMailsFromOutbox::runSendFromOutbox(&std::cout); });
                bool seen = false;
                for (unsigned i = 0; i < 300 && !seen; ++i) {
                    astra_mail::MailDeliveryState state; std::string error;
                    if (state.load(astra_mail::getOutboxRoot() / "target@example.invalid", error) && state.exists)
                        for (const auto& entry : state.recipients) seen = seen || entry.status == astra_mail::RecipientDelivery::Unknown;
                    if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                const bool denied = seen && !other.apply();
                worker.join();
                require(denied && tb.secretContext() == context && other.apply() && !tb.hasMailPassword(), "in-flight account change");
                std::cout << "INFLIGHT_SWITCH_BLOCKED=1\n";
            }
        } else return 2;
        return 0;
    } catch (const std::exception& error) { std::cerr << "AM02_DRIVER_FAILED: " << error.what() << "\n"; return 1; }
}
