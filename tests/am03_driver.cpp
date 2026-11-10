// Actual application libraries; all credentials and payloads are synthetic.
#include "Utils/MailTransport.h"
#include "Utils/ImapClient.h"
#include "Utils/SmtpClient.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/LocalProfileCache.h"
#include "core/idle/IdleBackground.h"
#include "core/mail/MailNetworkSettings.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include <dirent.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace astra_mail;
using Clock = std::chrono::steady_clock;
static unsigned setting(const char* name, unsigned fallback) {
    const char* value = std::getenv(name); return value ? std::stoul(value) : fallback;
}
static size_t descriptors() {
    DIR* directory = opendir("/proc/self/fd"); size_t count = 0;
    while (readdir(directory)) ++count;
    closedir(directory); return count;
}
int main(int argc, char** argv) {
    try {
        if (argc < 4) return 2;
        const std::string mode = argv[1];
        const int smtpPort = std::stoi(argv[2]), imapPort = std::stoi(argv[3]);
        net::Settings limits;
        limits.dnsMs = setting("DNS_MS", 300); limits.connectMs = setting("CONNECT_MS", 400);
        limits.smtpCommandMs = setting("COMMAND_MS", 500); limits.smtpDataStartMs = 500;
        limits.smtpFinalMs = setting("FINAL_MS", 1200); limits.ioIdleMs = setting("IO_MS", 350);
        limits.imapCommandMs = setting("IMAP_MS", 500); limits.imapAppendMs = setting("APPEND_MS", 1200);
        limits.keepIdleSeconds = 1; limits.keepIntervalSeconds = 1; limits.keepProbes = 2;
        limits.tcpUserMs = 2000;
        net::configure(limits);
        TbProfile profile; profile.id = "synthetic-am03";
        LocalMailSettings settings; settings.profileId = profile.id; settings.accountId = "synthetic";
        settings.email = settings.userName = "sender@example.invalid";
        settings.smtpHost = settings.imapHost = "127.0.0.1";
        settings.smtpPort = smtpPort; settings.imapPort = imapPort;
        auto& tb = ThunderbirdSettings::instance(); tb.loadFromLocalCache(profile, settings);
        tb.setMailPassword("synthetic-only");
        const auto start = Clock::now();
        if (mode == "outbox") {
            SendCycleReport report;
            const bool ok = CmdSendMailsFromOutbox::runSendFromOutbox(&std::cout, &report);
            std::cout << "COMPLETE=" << ok << " CONFIRMED=" << report.confirmedRecipients
                      << " QUEUED=" << report.queuedMessages << " AWAITING=" << report.awaitingRecipients
                      << " UNKNOWN=" << report.unconfirmedRecipients << " ERROR=" << report.lastError << "\n";
        } else if (mode == "background") {
            IdleBackground::instance().start();
            std::cout << "BACKGROUND_STARTED\n" << std::flush;
            std::string line; std::getline(std::cin, line);
            const auto stop = Clock::now(); IdleBackground::instance().stop();
            std::cout << "STOP_MS=" << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-stop).count() << "\n";
        } else if (mode == "smtp") {
            SmtpClient smtp("127.0.0.1", smtpPort); smtp.setCredentials(settings.userName, tb.mailPassword());
            SmtpClient::Session session; std::string error;
            bool ok = smtp.openSession(session, error);
            if (ok) { const auto result = smtp.deliver(session, argv[4], {"a@example.invalid"});
                ok = result.complete(); error = result.detail; }
            std::cout << "SMTP_OK=" << ok << " ERROR=" << error << "\n";
        } else if (mode == "imap-list") {
            std::vector<std::string> folders; std::string error;
            const bool ok = ImapClient::listMailboxes("127.0.0.1", imapPort, settings.userName, tb.mailPassword(), folders, error);
            std::cout << "IMAP_OK=" << ok << " FOLDERS=" << folders.size() << " ERROR=" << error << "\n";
        } else if (mode == "imap-fetch") {
            std::string body, error;
            const bool ok = ImapClient::fetchRawEmlByUid("127.0.0.1", imapPort, settings.userName, tb.mailPassword(), "INBOX", "1", body, error);
            std::cout << "IMAP_OK=" << ok << " SIZE=" << body.size() << " ERROR=" << error << "\n";
        } else if (mode == "imap-append") {
            ImapClient client(false); std::string error;
            const bool ok = client.imapAppendSent("Subject: synthetic\r\n\r\nsynthetic body\r\n", error);
            std::cout << "IMAP_OK=" << ok << " ERROR=" << error << "\n";
        } else if (mode == "cancel" || mode == "dns-cancel") {
            limits.smtpCommandMs = 20000; limits.dnsMs = 20000; net::configure(limits);
            std::thread worker([&]() {
                if (mode == "dns-cancel") { std::string error; auto socket = net::connectSocket("am03-slow.invalid", 25, error); }
                else { SmtpClient smtp("127.0.0.1", smtpPort); SmtpClient::Session session; std::string error; smtp.openSession(session,error); }
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            net::requestStop(); worker.join(); net::resetStop();
            std::cout << "CANCELLED=1\n";
        } else if (mode == "dns") {
            std::string error; auto socket = net::connectSocket("am03-slow.invalid", 25, error);
            std::cout << "DNS_OK=" << (socket >= 0) << " ERROR=" << error << "\n";
        } else if (mode == "fds") {
            const auto before = descriptors();
            for (int i = 0; i < 30; ++i) {
                try { SmtpClient smtp("127.0.0.1", smtpPort); SmtpClient::Session session;
                    smtp.setCredentials(settings.userName, tb.mailPassword()); std::string error;
                    smtp.openSession(session, error); throw std::runtime_error("synthetic unwind");
                } catch (const std::runtime_error&) {}
                ImapSession session; std::string error; imapOpen(session,"127.0.0.1",imapPort,error);
            }
            std::cout << "FD_DELTA=" << (descriptors() - before) << "\n";
        } else if (mode == "config") {
            const auto value = loadMailNetworkSettings();
            std::cout << "CONFIG=" << value.dnsMs << "," << value.connectMs << "," << value.smtpFinalMs
                      << "," << value.keepProbes << "\n";
        } else return 2;
        std::cout << "ELAPSED_MS=" << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count() << "\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
