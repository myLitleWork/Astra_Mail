// Native IMAP/SMTP/vault entry points. Inputs are synthetic hex-encoded fixtures.
#include "Utils/ImapClient.h"
#include "Utils/SmtpClient.h"
#include "Utils/SensitiveString.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/LocalProfileCache.h"
#include "secrets/SecretManager.h"
#include <iostream>
#include <stdexcept>
static std::string input() {
    std::string hex, value; std::getline(std::cin, hex);
    if (hex.size() % 2) throw std::runtime_error("invalid synthetic input");
    for (size_t i = 0; i < hex.size(); i += 2) value += static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16));
    return value;
}
int main(int argc,char** argv) {
    try {
        if(argc < 4) return 2;
        const std::string mode = argv[1];
        const int imapPort = std::stoi(argv[2]), smtpPort = std::stoi(argv[3]);
        const auto user = input(); auto password = input(); const auto folder = input();
        astra_mail::ClearSensitiveOnExit wipe{password};
        auto limits = astra_mail::net::currentSettings();
        limits.connectMs=500; limits.imapCommandMs=500; limits.ioIdleMs=500; limits.imapAppendMs=1000;
        astra_mail::net::configure(limits);
        astra_mail::TbProfile profile; profile.id="synthetic-am07";
        astra_mail::LocalMailSettings settings;
        settings.profileId=profile.id; settings.accountId="synthetic-am07";
        settings.email="sender@example.invalid"; settings.userName=user;
        settings.smtpHost=settings.imapHost="127.0.0.1";
        settings.imapPort=imapPort; settings.smtpPort=smtpPort;
        auto& tb=ThunderbirdSettings::instance();
        if (!tb.loadFromLocalCache(profile,settings) || !tb.setMailPassword(password)) throw std::runtime_error("synthetic account");
        bool ok=false; std::string error;
        if(mode=="login" || mode=="select" || mode=="select-api" || mode=="uid-fetch" || mode=="uid-store") {
            ImapSession session;
            ok=imapOpen(session,"127.0.0.1",imapPort,error) && imapLogin(session,user,password,error);
            if(ok && mode=="select") ok=imapSelect(session,folder,error);
            if(ok && mode=="select-api") ok=ImapClient::selectMailbox(session,folder,error);
            if(ok && mode=="uid-fetch") { std::string body; ok=ImapClient::fetchRawEmlByUid(session,folder,body,error); }
            if(ok && mode=="uid-store") ok=ImapClient::markSeenByUid(session,folder,error);
            imapClose(session);
        } else if(mode=="list") {
            std::vector<std::string> folders;
            ok=ImapClient::listMailboxes("127.0.0.1",imapPort,user,password,folders,error) && folders.size()==1;
        } else if(mode=="legacy-list") {
            std::string result; ok=ImapClient::listFolders("127.0.0.1",imapPort,user,password,result,error);
        } else if(mode=="search") {
            std::vector<std::string> uids;
            ok=ImapClient::searchUnreadUids("127.0.0.1",imapPort,user,password,folder,uids,error) && uids.size()==1;
        } else if(mode=="fetch") {
            std::string body;
            ok=ImapClient::fetchRawEmlByUid("127.0.0.1",imapPort,user,password,folder,"1",body,error) && body=="synthetic body\r\n";
        } else if(mode=="mark") {
            ok=ImapClient::markSeenByUid("127.0.0.1",imapPort,user,password,folder,"1",error);
        } else if(mode=="append") {
            ImapClient client(false); ok=client.imapAppendSent("Subject: synthetic\r\n\r\nsynthetic body\r\n",error);
        } else if(mode=="smtp") {
            SmtpClient client("127.0.0.1",smtpPort); client.setCredentials(user,password);
            SmtpClient::Session session; ok=client.openSession(session,error); SmtpClient::closeSession(session);
        } else if(mode=="stored" || mode=="seed") {
            auto& manager=SecretManager::instance();
            ok=manager.store(tb.secretContext(),password,"synthetic-am07-pin");
            if(mode=="seed") ok=ok && astra_mail::saveLocalMailSettings(settings) && astra_mail::saveLocalContacts({},{});
            else if(ok) {
                std::string restored; astra_mail::ClearSensitiveOnExit wipeRestored{restored};
                tb.clearMailPassword();
                ok=manager.load(tb.secretContext(),"synthetic-am07-pin",restored) && restored==password && tb.setMailPassword(restored);
                if(ok) { ImapClient client(false); ok=client.imapAppendSent("Subject: synthetic\r\n\r\nsynthetic body\r\n",error); }
            }
        } else return 2;
        std::cout << "OK=" << ok << " ERROR=" << error << "\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
