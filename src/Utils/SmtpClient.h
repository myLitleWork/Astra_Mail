#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include "MailTransport.h"

class SmtpClient
{
public:
    explicit SmtpClient(std::string  host, int port);
    ~SmtpClient();

    void setCredentials(const std::string& user,
                        const std::string& password);

    // ===== SMTP SESSION =====
    struct Session
    {
        astra_mail::net::Socket sock;
        std::string replyBuffer;
    };

    struct DeliveryResult
    {
        std::vector<std::string> delivered;
        std::vector<std::string> pending;
        std::vector<std::string> unknown;
        std::string detail;
        bool transportFailed = false;
        bool complete() const { return !delivered.empty() && pending.empty() && unknown.empty(); }
    };

    DeliveryResult deliver(Session& ses, const std::string& mimeFile,
                           const std::vector<std::string>& recipients);
    DeliveryResult deliver(Session& ses, FILE* mime,
                           const std::vector<std::string>& recipients);

    bool openSession(Session& ses, std::string& error);
    static void closeSession(Session& ses);

    bool sendMailFromFile(Session& ses,
                          const std::string& mimeFile,
                          const std::string& rcpt,
                          std::string& error);
    bool sendMailFromFile(Session& ses,
                          const std::string& mimeFile,
                          const std::vector<std::string>& rcpts,
                          std::string& error);

    bool sendMailFromFile(const std::string& mimeFile,
                          const std::string& rcpt,
                          std::string& error);
    bool sendMailFromFile(const std::string& mimeFile,
                          const std::vector<std::string>& rcpts,
                          std::string& error);

    static std::string base64(const std::string& in);

private:
    std::string host_;
    int port_;
    std::string user_, pass_;

    astra_mail::net::Socket socketConnect(std::string& error);

    static bool sendLine(int s, const std::string& text);
    static bool recvResponse(Session& ses, int& code, std::string& reply, unsigned timeoutMs = 0);
    static void abortSession(Session& ses);
};
