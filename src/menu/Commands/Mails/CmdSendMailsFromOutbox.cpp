#include "CmdSendMailsFromOutbox.h"
#include "MailFolderScanner.h"
#include "MailFolderReport.h"
#include "MailSendPipeline.h"
#include "src/Utils/Utils.h"
#include "ThunderbirdSettings.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "src/ContactsManager.h"
#include "UtilsFS/FsCompat.h"
#include "src/Utils/SmtpClient.h"

#include <iostream>
#include <stdexcept>

namespace astra_mail
{
    MailSendPipeline createPipelineFromThunderbird(); // определена в CmdSendMailsFromPath.cpp

    static fs::path getOutboxRoot()
    {
        const char* home = getenv("HOME");
        if (!home)
            throw std::runtime_error("HOME not set");

        fs::path root = fs::path(home) / ".local/share/AstraMail/outbox";
        return root;
    }

    CommandResult CmdSendMailsFromOutbox::execute()
    {
        auto outboxPath = getOutboxRoot();

        // Проверка существования outbox
        if (!fs::exists(outboxPath))
        {
            std::cout << "Outbox не существует или пуст.\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        // Сканирование outbox
        auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());
        MailFolderScanner scanner({true,true,false}, resolver);
        auto localResult = scanner.scanMount(outboxPath);

        MailFolderReport::print(std::cout, localResult);

        if(localResult.mails.empty())
        {
            std::cout << "В outbox нет писем для отправки.\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        auto& tb = ThunderbirdSettings::instance();

        if (!tb.active().valid())
        {
            std::cout << "Почтовый аккаунт не выбран.\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        MailSendPipeline pipe = createPipelineFromThunderbird();

        // SMTP создаётся ИЗ пайплайна
        SmtpClient smtp(pipe.smtpHost(), pipe.smtpPort());
        smtp.setCredentials(pipe.smtpUser(), pipe.smtpPass());

        SmtpClient::Session ses;
        std::string err;

        if (!smtp.openSession(ses, err))
        {
            std::cout << "\n[SMTP] session open failed: " << err << "\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        for (auto& mail : localResult.mails)
        {
            std::cout << "\n[ОТПРАВКА] " << mail.recipient << " ...";
            std::cout.flush();

            std::string log;

            bool ok = pipe.send(mail, log, smtp, ses);

            std::cout << "\r";

            if (ok)
            {
                std::cout << "[ОТПРАВЛЕНО] " << mail.recipient << " ✓    ";
                fs::remove_all(mail.folderPath);
            }
            else
            {
                std::cout << "[НЕ ОТПРАВЛЕНО] " << mail.recipient << " ✗    ";
            }

            std::cout.flush();
        }

        smtp.closeSession(ses);

        my::pause();
        return CommandResult::ExitMenu;
    }

    CmdSendMailsFromOutbox::CmdSendMailsFromOutbox() : ICommand()
    {
    }

    std::string CmdSendMailsFromOutbox::title() const {
        return "Отправить письма из outbox";
    }
}
