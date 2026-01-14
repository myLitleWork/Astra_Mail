#include "ImapTester.h"
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static void sendln(int s, const std::string& txt)
{
    std::string out = txt + "\r\n";
    send(s, out.c_str(), out.size(), 0);
}

static std::string recvAll(int s)
{
    char buf[4096];
    ssize_t n = recv(s, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "";
    buf[n] = 0;
    return {buf};
}

void DiagnosticIMAP::runImapTest()
{
    std::string host = "dvkr.net";
    int port = 143;

    std::string user = "vogcentrcvo";
    std::string pass = "12345678";

    std::cout << "\n===== IMAP DIAGNOSTIC TEST =====\n";
    std::cout << "Server: " << host << ":" << port << "\n\n";

    // DNS resolve
    addrinfo hints{}, *res;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    {
        std::cout << "DNS ERROR\n";
        return;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
    {
        std::cout << "Socket error\n";
        return;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
    {
        std::cout << "Connection error\n";
        return;
    }

    std::cout << "Connected!\n\n";

    // Banner
    std::cout << "SERVER BANNER:\n" << recvAll(sock) << "\n";

    // LOGIN
    sendln(sock, "a1 LOGIN " + user + " " + pass);
    std::cout << "LOGIN RESPONSE:\n" << recvAll(sock) << "\n";

    // LIST
    sendln(sock, R"(a2 LIST "" "*")");
    std::cout << "LIST RESPONSE:\n" << recvAll(sock) << "\n";

    // SELECT Sent
    sendln(sock, "a3 SELECT \"Sent\"");
    std::cout << "SELECT RESPONSE:\n" << recvAll(sock) << "\n";

    // Тестовое письмо
    std::string mail =
            "From: test@dvkr.net\r\n"
            "To: test@dvkr.net\r\n"
            "Subject: IMAP APPEND TEST\r\n"
            "\r\n"
            "TEST BODY\r\n";

    std::string cmd = "a4 APPEND \"Sent\" {" + std::to_string(mail.size()) + "}";
    sendln(sock, cmd);

    std::string resp = recvAll(sock);
    std::cout << "APPEND SIZE RESPONSE:\n" << resp << "\n";

    if (resp.find('+') != std::string::npos)
    {
        send(sock, mail.c_str(), mail.size(), 0);
        std::cout << "-- MAIL BODY SENT --\n";
        std::cout << "APPEND FINAL:\n" << recvAll(sock) << "\n";
    }
    else
    {
        std::cout << "Server did NOT accept literal body.\n";
    }

    // LOGOUT
    sendln(sock, "a5 LOGOUT");
    std::cout << "LOGOUT RESPONSE:\n" << recvAll(sock) << "\n";

    close(sock);

    std::cout << "\n===== END OF IMAP TEST =====\n";
}
