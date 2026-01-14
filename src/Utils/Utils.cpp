#include "Utils.h"

#include "FlashDriveScanner.h"
#include "SmtpClient.h"
#include "EmailMessage.h"
#include "ImapClient.h"

#include <iostream>
#include <limits>
#include <vector>


#if __has_include(<filesystem>)
#include <filesystem>
  namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif


#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>



namespace astra_mail
{
    void sendCmd(int sock, const std::string& cmd)
    {
        std::string full = cmd + "\r\n";
        ::send(sock, full.c_str(), full.size(), 0);
    }

    int extractLastUID(const std::string& resp)
    {
        int last = -1;
        std::string num;

        for (char c : resp)
        {
            if (isdigit(static_cast<unsigned char>(c)))
                num += c;
            else
            {
                if (!num.empty())
                {
                    int v = std::stoi(num);
                    if (v > last) last = v;
                    num.clear();
                }
            }
        }

        if (!num.empty())
        {
            int v = std::stoi(num);
            if (v > last) last = v;
        }

        return last;
    }

    std::string recvUntil(int sock, const std::string& stopToken)
    {
        std::string all;
        char buf[4096];

        while (true)
        {
            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;

            buf[n] = 0;
            all += buf;

            if (all.find(stopToken) != std::string::npos)
                break;
        }
        return all;
    }

    int connectTcp(const std::string& host, int port)
    {
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family   = AF_UNSPEC;

        addrinfo* res = nullptr;
        std::string portStr = std::to_string(port);

        int rc = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
        if (rc != 0)
        {
            std::cerr << "getaddrinfo error: " << gai_strerror(rc) << "\n";
            return -1;
        }

        int sock = -1;
        for (addrinfo* p = res; p; p = p->ai_next)
        {
            sock = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock < 0) continue;

            if (::connect(sock, p->ai_addr, p->ai_addrlen) == 0)
                break;

            ::close(sock);
            sock = -1;
        }

        ::freeaddrinfo(res);
        return sock;
    }

    void showFlashInfo()
    {
        FlashDriveScanner scanner;
        scanner.scan();
        const auto& drives = scanner.getDrives();

        if (drives.empty())
        {
            std::cout << "\nНет подключённых флешек.\n";
            return;
        }

        std::cout << "\n=== Найденные USB-накопители ===\n";
        int index = 1;
        for (const auto& d : drives)
        {
            std::cout << index++ << ". "
                      << d.device << " | " << d.label
                      << " | FS=" << d.fsType
                      << " | " << (d.mounted ? "mounted" : "not mounted")
                      << " | MP=" << d.mountPoint
                      << "\n";
        }
    }

    std::string chooseFileFromFlash()
    {
        FlashDriveScanner scanner;
        scanner.scan();
        const auto& drives = scanner.getDrives();

        if (drives.empty())
        {
            std::cout << "\nФлешек не найдено.\n";
            return "";
        }

        std::cout << "\nВыберите флешку:\n";
        for (size_t i = 0; i < drives.size(); i++)
            std::cout << (i + 1) << ") " << drives[i].device << " - " << drives[i].mountPoint << "\n";

        int choice;
        std::cout << "Номер: ";
        std::cin >> choice;

        if (choice < 1 || choice > (int)drives.size())
        {
            std::cout << "Неверный выбор.\n";
            return "";
        }

        auto mp = drives[choice - 1].mountPoint;
        if (mp.empty())
        {
            std::cout << "Флешка не смонтирована.\n";
            return "";
        }

        std::cout << "\nВведите имя файла относительно каталога: " << mp << "\nФайл: ";
        std::string fname;
        std::cin >> fname;

        return mp + "/" + fname;
    }

    void sendTestEmail()
    {
        std::cout << "\n=== Отправка тестового письма ===\n";

        EmailMessage msg;
        msg.from    = "vogcentrcvo@dvkr.net";
        msg.to      = "vogcentrcvo@dvkr.net";
        msg.subject = "Test from Astra C++";
        msg.body    = "Hello! This mail was sent using SmtpClient test mode.\n";

        SmtpClient smtp("dvkr.net", 25);
        smtp.setCredentials("vogcentrcvo", "12345678");

        std::string err;
        if (!smtp.sendMail(msg, err))
            std::cout << "Ошибка отправки: " << err << "\n";
        else
            std::cout << "Тестовое письмо отправлено успешно!\n";
    }

    void cleanupGarbage()
    {
        FlashDriveScanner::cleanupGarbageMounts();
        std::cout << "Мусорные каталоги удалены.\n";
    }

    void clearScreen()
    {
        std::cout << "\033[2J\033[1;1H";
    }

    std::string selectAttachmentFromFlash()
    {
        FlashDriveScanner scanner;
        scanner.scan();
        const auto& drives = scanner.getDrives();

        if (drives.empty())
        {
            std::cout << "\nФлешек не найдено.\n";
            return "";
        }

        clearScreen();
        std::cout << "=== Выберите флешку ===\n";
        for (size_t i = 0; i < drives.size(); i++)
        {
            std::cout << (i + 1) << ") " << drives[i].device
                      << "  [" << drives[i].mountPoint << "]\n";
        }
        std::cout << "0) Назад\n";

        int choice;
        std::cout << "Ваш выбор: ";
        std::cin >> choice;

        if (choice == 0) return "";
        if (choice < 1 || choice > (int)drives.size())
        {
            std::cout << "Неверный выбор.\n";
            return "";
        }

        std::string mp = drives[choice - 1].mountPoint;
        if (mp.empty())
        {
            std::cout << "Флешка не смонтирована.\n";
            return "";
        }

        std::vector<std::string> files;

        clearScreen();
        std::cout << "=== Файлы на флешке: " << mp << " ===\n";

        for (auto& p : fs::directory_iterator(mp))
        {
            if (fs::is_regular_file(p))
                files.push_back(p.path().string());
        }

        if (files.empty())
        {
            std::cout << "Нет файлов.\n";
            return "";
        }

        for (size_t i = 0; i < files.size(); i++)
            std::cout << (i + 1) << ") " << files[i] << "\n";

        std::cout << "0) Назад\n";
        std::cout << "Выберите файл: ";
        std::cin >> choice;

        if (choice == 0) return "";
        if (choice < 1 || choice > (int)files.size())
        {
            std::cout << "Неверный выбор.\n";
            return "";
        }

        return files[choice - 1];
    }

    void listImapFolders()
    {
        std::string result, err;

        if (ImapClient::listFolders("imap.dvkr.net", 143, "vogcentrcvo", "12345678", result, err))
        {
            std::cout << "\n=== Folders on IMAP server ===\n";
            std::cout << result << "\n";
        }
        else
        {
            std::cout << "Ошибка: " << err << "\n";
        }
    }

    namespace my
    {
        void pause()
        {
            std::cout << "\nНажмите Enter для продолжения...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    void runImapSyncTest()
    {
        clearScreen();
        std::cout << "===== IMAP SYNC TEST =====\n";

        const char* server = "dvkr.net";
        int port = 143;

        std::string user = "vogcentrcvo";
        std::string pass = "12345678";

        addrinfo hints{}, *res;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(server, std::to_string(port).c_str(), &hints, &res) != 0)
        {
            std::cout << "DNS ERROR\n";
            return;
        }

        int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0) { std::cout << "SOCKET ERROR\n"; return; }

        if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
        {
            std::cout << "CONNECT ERROR\n";
            return;
        }
        freeaddrinfo(res);

        std::cout << "Connected!\n";

        std::string banner = recvUntil(sock, "\n");
        std::cout << "BANNER:\n" << banner << "\n";

        sendCmd(sock, "a1 LOGIN " + user + " " + pass);
        std::string loginResp = recvUntil(sock, "a1 OK");
        std::cout << "LOGIN RESPONSE:\n" << loginResp << "\n";

        sendCmd(sock, R"(a2 LIST "" "*")");
        std::string listResp = recvUntil(sock, "a2 OK");
        std::cout << "LIST RESPONSE:\n" << listResp << "\n";

        sendCmd(sock, "a3 SELECT Sent");
        std::string selectResp = recvUntil(sock, "a3 OK");
        std::cout << "SELECT RESPONSE:\n" << selectResp << "\n";

        sendCmd(sock, "a4 UID SEARCH ALL");
        std::string searchResp = recvUntil(sock, "a4 OK");
        std::cout << "SEARCH RESPONSE:\n" << searchResp << "\n";

        int lastUID = extractLastUID(searchResp);
        if (lastUID <= 0)
        {
            std::cout << "Не удалось определить последний UID!\n";
        }
        else
        {
            std::cout << "Последний UID: " << lastUID << "\n";
            sendCmd(sock, "a5 UID FETCH " + std::to_string(lastUID) + " (BODY.PEEK[])");
            std::string fetchResp = recvUntil(sock, "a5 OK");
            std::cout << "FETCH RESPONSE:\n" << fetchResp << "\n";
        }

        sendCmd(sock, "a6 LOGOUT");
        std::string logoutResp = recvUntil(sock, "OK");
        std::cout << "LOGOUT:\n" << logoutResp << "\n";

        std::cout << "===== IMAP SYNC DONE =====\n";
        my::pause();
    }

    uint32_t rnd()
    {
        static thread_local std::mt19937 gen{std::random_device{}()};
        static thread_local std::uniform_int_distribution<uint32_t> dist;
        return dist(gen);
    }

    bool imapAppendSent(const std::string& rawMime, std::string& err)
    {
        // (оставь твою текущую реализацию здесь без изменений,
        //  просто перенеси 1-в-1 из Utils.h в Utils.cpp)
        // ВАЖНО: внутри не делай новых глобальных функций.
        // ---- вставь целиком тело из твоего Utils.h ----

        const char* host = "dvkr.net";
        int port = 143;
        const char* user = "vogcentrcvo";
        const char* pass = "12345678";

        std::cout << "\n[IMAP] Connecting to " << host << ":" << port << "...\n";

        addrinfo hints{}, *res = nullptr;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0)
        {
            err = "DNS error";
            return false;
        }

        int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0)
        {
            freeaddrinfo(res);
            err = "socket() failed";
            return false;
        }

        if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
        {
            freeaddrinfo(res);
            close(sock);
            err = "connect() failed";
            return false;
        }
        freeaddrinfo(res);

        auto recvUntilLocal = [&](const std::string& token)
        {
            std::string all;
            char buf[2048];

            while (true)
            {
                ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
                if (n <= 0) break;

                buf[n] = 0;
                all += buf;

                std::cout << "[IMAP] RECV: " << buf;
                if (all.find(token) != std::string::npos)
                    break;
            }
            return all;
        };

        auto sendCmdLocal = [&](const std::string& s)
        {
            std::string line = s + "\r\n";
            std::cout << "[IMAP] SEND: " << line;
            send(sock, line.c_str(), line.size(), 0);
        };

        recvUntilLocal("\n");

        sendCmdLocal("a1 LOGIN " + std::string(user) + " " + std::string(pass));
        recvUntilLocal("a1 OK");

        sendCmdLocal("a2 SELECT Sent");
        recvUntilLocal("a2 OK");

        size_t sz = rawMime.size();
        std::string cmd = "a3 APPEND Sent (\\Seen) {" + std::to_string(sz) + "}";
        sendCmdLocal(cmd);

        std::string resp = recvUntilLocal("+");
        if (resp.find('+') == std::string::npos)
        {
            err = "Server did not accept literal body";
            close(sock);
            return false;
        }

        send(sock, rawMime.c_str(), rawMime.size(), 0);
        send(sock, "\r\n", 2, 0);

        std::string r2 = recvUntilLocal("a3 OK");
        if (r2.find("a3 OK") == std::string::npos)
        {
            err = "APPEND failed: " + r2;
            close(sock);
            return false;
        }

        sendCmdLocal("a4 LOGOUT");
        recvUntilLocal("OK");

        close(sock);
        return true;
    }
}
