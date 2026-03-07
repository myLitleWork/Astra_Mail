#include "CmdSendMailsFromOutbox.h"
#include "IdleBackground.h"
#include "MailFolderScanner.h"
#include "MailFolderReport.h"
#include "MailSendPipeline.h"
#include "src/Utils/Utils.h"
#include "ThunderbirdSettings.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "src/ContactsManager.h"
#include "UtilsFS/FsCompat.h"
#include "src/Utils/SmtpClient.h"
#include "AstraMailPaths.h"

#include <iostream>
#include <stdexcept>

namespace astra_mail
{
    MailSendPipeline createPipelineFromThunderbird(); // определена в CmdSendMailsFromPath.cpp

    bool CmdSendMailsFromOutbox::runSendFromOutbox(std::ostream* log)
    {
        auto outboxPath = getOutboxRoot();
        if (!fs::exists(outboxPath))
            return true;

        auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());
        MailFolderScanner scanner({true, true, false}, resolver);
        auto localResult = scanner.scanMount(outboxPath);

        if (localResult.mails.empty())
        {
            if (log) *log << "Папка исходящих пуста. Добавьте письма в ~/.local/share/AstraMail/outbox/<получатель>/\n";
            return true;
        }

        if (log)
            MailFolderReport::print(*log, localResult);

        auto& tb = ThunderbirdSettings::instance();
        if (!tb.active().valid())
        {
            if (log) *log << "[Ошибка] Почтовый аккаунт не выбран. Выберите аккаунт в профиле Thunderbird.\n";
            return false;
        }

        if (!tb.hasMailPassword())
        {
            if (log) *log << "[Ошибка] Пароль почты не задан. Введите PIN в меню безопасности.\n";
            return false;
        }

        MailSendPipeline pipe = createPipelineFromThunderbird();
        SmtpClient smtp(pipe.smtpHost(), pipe.smtpPort());
        smtp.setCredentials(pipe.smtpUser(), pipe.smtpPass());

        SmtpClient::Session ses;
        std::string err;
        if (!smtp.openSession(ses, err))
        {
            if (log) *log << "[Ошибка] SMTP недоступен: " << err << "\n";
            return false;
        }

        for (auto& mail : localResult.mails)
        {
            if (log) *log << "\n[ОТПРАВКА] " << mail.recipient << " ..." << std::flush;

            std::string sendLog;
            bool ok = pipe.send(mail, sendLog, smtp, ses);

            if (ok)
            {
                if (log) *log << "\r[ОТПРАВЛЕНО] " << mail.recipient << " ✓    ";
                fs::remove_all(mail.folderPath);
            }
            else if (log)
                *log << "\r[НЕ ОТПРАВЛЕНО] " << mail.recipient << " ✗    ";

            if (log) *log << std::flush;
        }

        SmtpClient::closeSession(ses);
        return true;
    }

    CommandResult CmdSendMailsFromOutbox::execute()
    {
        IdleBackground::BusyGuard busy;
        bool ok = runSendFromOutbox(&std::cout);
        if (!ok)
            std::cout << "[Ошибка] Отправка не выполнена. Проверьте аккаунт и соединение с сервером.\n";
        my::pause();
        return CommandResult::ExitMenu;
    }

    CmdSendMailsFromOutbox::CmdSendMailsFromOutbox() : ICommand()
    {
    }

    std::string CmdSendMailsFromOutbox::title() const {
        return "Отправить письма из исходящих";
    }
}
