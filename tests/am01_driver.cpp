// Native integration entry points, linked to the same libraries as AstraMail.
#include "Utils/SmtpClient.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/LocalProfileCache.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "menu/Commands/Mails/CmdReviewDelivery.h"
#include "menu/Commands/Mails/CmdSendMailsFromPath.h"
#include "secrets/SecretManager.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc < 4) return 2;
        const std::string mode = argv[1];
        astra_mail::TbProfile profile;
        profile.id = "synthetic-am01";
        astra_mail::LocalMailSettings settings;
        settings.profileId = profile.id;
        settings.accountId = "synthetic-account";
        settings.email = settings.userName = "sender@example.invalid";
        settings.imapHost = settings.smtpHost = "127.0.0.1";
        settings.smtpPort = std::stoi(argv[2]); settings.imapPort = std::stoi(argv[3]);
        auto& tb = ThunderbirdSettings::instance();
        tb.loadFromLocalCache(profile, settings);
        tb.setMailPassword("synthetic-only");
        if (mode == "outbox") {
            const bool ok = astra_mail::CmdSendMailsFromOutbox::runSendFromOutbox(&std::cout);
            std::cout << "\nALL_COMPLETE=" << ok << "\n";
        } else if (mode == "review") {
            astra_mail::CmdReviewDelivery command;
            command.execute();
        } else if (mode == "import" && argc == 5) {
            astra_mail::CmdSendMailsFromPath command(argv[4]);
            command.execute();
        } else if (mode == "crypto" || mode == "seed") {
            auto& manager = SecretManager::instance();
            std::string password;
            const bool ok = manager.store(tb.secretContext(), "synthetic-only", "synthetic-pin-123456") &&
                manager.load(tb.secretContext(), "synthetic-pin-123456", password) && password == "synthetic-only";
            std::cout << "REAL_SODIUM_ROUNDTRIP=" << ok << "\n";
            if (!ok) return 1;
            if (mode == "seed") {
                if (!astra_mail::saveLocalMailSettings(settings) ||
                    !astra_mail::saveLocalContacts({}, {})) return 1;
            }
        } else if (mode == "smtp" && argc >= 6) {
            SmtpClient smtp("127.0.0.1", settings.smtpPort);
            smtp.setCredentials(settings.userName, tb.mailPassword());
            SmtpClient::Session session;
            std::string error;
            if (!smtp.openSession(session, error)) {
                std::cout << "AUTH_FAILED=" << (session.sock == -1) << " " << error << "\n";
            } else {
                std::vector<std::string> recipients;
                for (int i = 5; i < argc; ++i) recipients.push_back(argv[i]);
                const auto result = smtp.deliver(session, argv[4], recipients);
                for (const auto& address : result.delivered) std::cout << "D=" << address << "\n";
                for (const auto& address : result.pending) std::cout << "P=" << address << "\n";
                for (const auto& address : result.unknown) std::cout << "U=" << address << "\n";
                std::cout << "DETAIL=" << result.detail << "\n";
                SmtpClient::closeSession(session);
            }
        } else return 2;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n"; return 1;
    }
}
