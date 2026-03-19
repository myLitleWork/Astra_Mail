#include "MailSendPipeline.h"
#include "Resolver/Resolvers/OutgoingSubjectResolver.h"
#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "Utils/EmailMessage.h"
#include "Utils/ImapClient.h"
#include "core/mime/MimeFileBuilder.h"
#include <iostream>
#include "UtilsFS/FsCompat.h"
#include <cctype>




namespace astra_mail
{
    static std::string trimAscii(std::string s)
    {
        auto is_ws = [](unsigned char c){ return std::isspace(c); };
        while (!s.empty() && is_ws((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && is_ws((unsigned char)s.back())) s.pop_back();
        return s;
    }

    static std::vector<std::string> splitRecipients(const std::string& s)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char ch : s)
        {
            if (ch == ',' || ch == ';')
            {
                cur = trimAscii(cur);
                if (!cur.empty())
                    out.push_back(cur);
                cur.clear();
            }
            else
            {
                cur.push_back(ch);
            }
        }
        cur = trimAscii(cur);
        if (!cur.empty())
            out.push_back(cur);
        return out;
    }

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
        const auto recipients = splitRecipients(info.recipient);
        if (recipients.empty())
        {
            log = "No recipients provided";
            return false;
        }

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

        std::vector<std::string> attachmentNames;
        for (const auto& a : info.attachments)
            attachmentNames.push_back(a.name);

        config::ensureOutgoingSubjectRules(false);
        OutgoingSubjectResolver subjectResolver(config::outgoingSubjectRules());
        subjectResolver.load();

        std::string subject = subjectResolver.resolve(attachmentNames);

        EmailMessage msg;
        msg.from    = user_;
        msg.to      = info.recipient;
        msg.subject = subject;
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

        if (!smtp.sendMailFromFile(mimeFile, recipients, log))
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
            std::cout << "\n[ВНИМАНИЕ] Письмо отправлено, но НЕ добавлено в папку \"Отправленные\": "
                      << err << "\n";
            log = "OK: SMTP sent, but IMAP APPEND failed: " + err;
            return true;
        }

        if (verbose)
            std::cout << "\r[PIPE] ✓ done                         \n";

        log = "OK: sent to " + std::to_string(recipients.size()) + " recipient(s)";
        return true;
    }

    bool MailSendPipeline::send(const MailFolderInfo& info,
                                std::string& log,
                                SmtpClient& smtp,
                                SmtpClient::Session& ses,
                                bool verbose /* = false */)
    {
        const auto recipients = splitRecipients(info.recipient);
        if (recipients.empty())
        {
            log = "No recipients provided";
            return false;
        }

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

        std::vector<std::string> attachmentNames;
        for (const auto& a : info.attachments)
            attachmentNames.push_back(a.name);

        config::ensureOutgoingSubjectRules(false);
        OutgoingSubjectResolver subjectResolver(config::outgoingSubjectRules());
        subjectResolver.load();

        std::string subject = subjectResolver.resolve(attachmentNames);

        EmailMessage msg;
        msg.from    = user_;
        msg.to      = info.recipient;
        msg.subject = subject;
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

        if (!smtp.sendMailFromFile(ses, mimeFile, recipients, log))
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
            std::cout << "\n[ВНИМАНИЕ] Письмо отправлено, но НЕ добавлено в папку \"Отправленные\": "
                      << err << "\n";
            log = "OK: SMTP sent, but IMAP APPEND failed: " + err;
            return true;
        }

        if (verbose)
            std::cout << "\r[PIPE] ✓ done                         \n";

        log = "OK: sent to " + std::to_string(recipients.size()) + " recipient(s)";
        return true;
    }





}
