#pragma once
#include "core/mail/MailFolderModels.h"
#include "Utils/SmtpClient.h"
#include "Utils/ImapClient.h"
#include "core/mime/MimeFileBuilder.h"
#include <string>

namespace astra_mail
{
    struct MailAttemptResult {
        size_t confirmed = 0;
        size_t unconfirmed = 0;
        bool transportFailed = false;
        std::string warning;
    };
    class MailSendPipeline
    {
    public:

        const std::string& smtpHost() const { return smtpHost_; }
        int smtpPort() const { return smtpPort_; }
        const std::string& smtpUser() const { return user_; }
        const std::string& smtpPass() const { return pass_; }


        MailSendPipeline(const std::string& smtpHost, int smtpPort,
                         const std::string& imapHost, int imapPort,
                         const std::string& user, const std::string& pass);
        ~MailSendPipeline();

        bool send(const MailFolderInfo& info, std::string& log, bool verbose = false);

        bool send(const MailFolderInfo& info,
                  std::string& log,
                  SmtpClient& smtp,
                  SmtpClient::Session& ses,
                  bool verbose = false,
                  MailAttemptResult* result = nullptr);


    private:
        std::string accountContext_;
        std::string smtpHost_, imapHost_;
        int smtpPort_, imapPort_;
        std::string user_, pass_;
    };




}
