#pragma once
#include <string>
#include "EmailMessage.h"

class SmtpClient
{
public:
    explicit SmtpClient(std::string  host, int port);

    void setCredentials(const std::string& user,
                        const std::string& password);

    // ===== SMTP SESSION =====
    struct Session
    {
        int sock = -1;
    };

    bool openSession(Session& ses, std::string& error);
    static void closeSession(Session& ses);

    static bool sendMail(Session& ses,
                  const EmailMessage& msg,
                  std::string& error);

    bool sendMailFromFile(Session& ses,
                          const std::string& mimeFile,
                          const std::string& rcpt,
                          std::string& error);

    // ===== СТАРЫЙ API (совместимость) =====
    bool sendMail(const EmailMessage& msg, std::string& error);
    bool sendMailFromFile(const std::string& mimeFile,
                          const std::string& rcpt,
                          std::string& error);

    static std::string buildRawMime(const EmailMessage& msg);
    static std::string base64(const std::string& in);

private:
    std::string host_;
    int port_;
    std::string user_, pass_;

    int socketConnect(std::string& error);

    static bool sendLine(int s, const std::string& text);
    static bool recvResponse(int s, std::string& reply);
};
