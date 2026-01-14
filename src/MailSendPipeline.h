#pragma once
#include "MailFolderModels.h"
#include "src/Utils/SmtpClient.h"
#include "src/Utils/ImapClient.h"
#include "MimeFileBuilder.h"
#include <string>

namespace astra_mail
{
    class MailSendPipeline
    {
    public:

        [[nodiscard]] const std::string& smtpHost() const { return smtpHost_; }
        [[nodiscard]] int smtpPort() const { return smtpPort_; }
        [[nodiscard]] const std::string& smtpUser() const { return user_; }
        [[nodiscard]] const std::string& smtpPass() const { return pass_; }


        MailSendPipeline(const std::string& smtpHost, int smtpPort,
                         const std::string& imapHost, int imapPort,
                         const std::string& user, const std::string& pass);

        bool send(const MailFolderInfo& info, std::string& log, bool verbose = false);

        bool send(const MailFolderInfo& info,
                  std::string& log,
                  SmtpClient& smtp,
                  SmtpClient::Session& ses,
                  bool verbose = false);


    private:
        std::string smtpHost_, imapHost_;
        int smtpPort_, imapPort_;
        std::string user_, pass_;
    };




}
