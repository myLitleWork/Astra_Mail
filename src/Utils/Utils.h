#pragma once
#include <string>
#include <ctime>
#include <unistd.h>
#include <climits>
#include <string>
#include <thread>
#include <random>
namespace astra_mail

{


    uint32_t rnd();

    // low-level IMAP helpers
    void sendCmd(int sock, const std::string& cmd);
    int  extractLastUID(const std::string& resp);
    std::string recvUntil(int sock, const std::string& stopToken);
    int  connectTcp(const std::string& host, int port);

    // misc
    void showFlashInfo();
    std::string chooseFileFromFlash();
    void sendTestEmail();
    void cleanupGarbage();
    void clearScreen();
    std::string selectAttachmentFromFlash();
    void listImapFolders();
    void runImapSyncTest();

    bool imapAppendSent(const std::string& rawMime, std::string& err);




    namespace my {

        inline std::string formatTime(std::time_t t)
        {
            if (t == 0)
                return "нет данных";

            std::tm tm{};
            localtime_r(&t, &tm);

            char buf[32];
            std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tm);
            return buf;
        }
        void pause();
    }



}
