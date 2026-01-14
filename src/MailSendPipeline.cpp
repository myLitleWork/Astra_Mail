#include "MailSendPipeline.h"
#include "src/Utils/EmailMessage.h"
#include "src/Utils/ImapClient.h"
#include "MimeFileBuilder.h"
#include <iostream>
#include "UtilsFS/FsCompat.h"




namespace astra_mail
{
    MailSendPipeline::MailSendPipeline(
            const std::string& smtpHost, int smtpPort,
            const std::string& imapHost, int imapPort,
            const std::string& user, const std::string& pass)
            : smtpHost_(smtpHost),
              imapHost_(imapHost),
              smtpPort_(smtpPort),
              imapPort_(imapPort),
              user_(user),
              pass_(pass)
    {}



    bool MailSendPipeline::send(const MailFolderInfo& info,
                                std::string& log,
                                bool verbose /* = false */)
    {
        int spin = 0;
        const char spinner[] = "|/-\\";

        auto status = [&](const std::string& s)
        {
            if (!verbose) return;
            std::cout << "\r[PIPE] " << spinner[spin++ % 4]
                      << " " << s << "                     "
                      << std::flush;
        };

        auto out = [&](const std::string& s)
        {
            if (verbose)
                std::cout << s;
        };

        status("prepare message");

        EmailMessage msg;
        msg.from    = user_;
        msg.to      = info.recipient;
        msg.subject = "ШТ";
        msg.body    = "Во вложении документы.";

        msg.attachments.clear();
        for (const auto& a : info.attachments)
        {
            EmailAttachment att;
            att.filename = a.name;
            att.path     = a.fullPath;
            msg.attachments.push_back(std::move(att));
        }

        // 1) MIME
        status("build MIME file");

        const std::string mimeFile = "/tmp/astramail.mime";
        if (!MimeFileBuilder::buildToFile(msg, mimeFile, log))
        {
            log = "MIME build error: " + log;
            return false;
        }

        // 2) SMTP
        status("SMTP send");

        SmtpClient smtp(smtpHost_, smtpPort_);
        smtp.setCredentials(user_, pass_);

        if (!smtp.sendMailFromFile(mimeFile, msg.to, log))
        {
            log = "SMTP error: " + log;
            return false;
        }

        // 3) IMAP APPEND
        status("IMAP append");
        ImapClient imap(false);
        std::string err;
        if (!imap.imapAppendSentFromFile(mimeFile, err))
        {
            log = "IMAP APPEND error: " + err;
            return false;
        }

        if (verbose)
            std::cout << "\r[PIPE] ✓ done                         \n";

        log = "OK: sent to " + msg.to;
        return true;
    }

    bool MailSendPipeline::send(const MailFolderInfo& info,
                                std::string& log,
                                SmtpClient& smtp,
                                SmtpClient::Session& ses,
                                bool verbose /* = false */)
    {
        int spin = 0;
        const char spinner[] = "|/-\\";

        auto status = [&](const std::string& s)
        {
            if (!verbose) return;
            std::cout << "\r[PIPE] " << spinner[spin++ % 4]
                      << " " << s << "                     "
                      << std::flush;
        };

        auto out = [&](const std::string& s)
        {
            if (verbose)
                std::cout << s;
        };

        status("prepare message");

        EmailMessage msg;
        msg.from    = user_;
        msg.to      = info.recipient;
        msg.subject = "ШТ";
        msg.body    = "Во вложении документы.";

        msg.attachments.clear();
        for (const auto& a : info.attachments)
        {
            EmailAttachment att;
            att.filename = a.name;
            att.path     = a.fullPath;
            msg.attachments.push_back(std::move(att));
        }

        // 1) MIME
        status("build MIME file");

        const std::string mimeFile = "/tmp/astramail.mime";
        if (!MimeFileBuilder::buildToFile(msg, mimeFile, log))
        {
            log = "MIME build error: " + log;
            return false;
        }

        // 2) SMTP (ИЗМЕНЕНО: без создания клиента и без логина)
        status("SMTP send");

        if (!smtp.sendMailFromFile(ses, mimeFile, msg.to, log))
        {
            log = "SMTP error: " + log;
            return false;
        }

        // 3) IMAP APPEND — БЕЗ ИЗМЕНЕНИЙ
        status("IMAP append");

        ImapClient imap(false);
        std::string err;
        if (!imap.imapAppendSentFromFile(mimeFile, err))
        {
            log = "IMAP APPEND error: " + err;
            return false;
        }

        if (verbose)
            std::cout << "\r[PIPE] ✓ done                         \n";

        log = "OK: sent to " + msg.to;
        return true;
    }





}
