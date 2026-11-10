#include "menu/MailCommandAccess.h"
#include "CmdSendMailsFromOutbox.h"
#include "core/mail/AccountAccess.h"
#include "core/idle/IdleBackground.h"
#include "core/mail/MailFolderScanner.h"
#include "core/mail/MailFolderReport.h"
#include "core/mail/MailSendPipeline.h"
#include "core/mail/MailDeliveryState.h"
#include <algorithm>
#include <set>
#include <sstream>
#include "Utils/Utils.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "core/contacts/ContactsManager.h"
#include "UtilsFS/FsCompat.h"
#include "Utils/SmtpClient.h"
#include "core/paths/AstraMailPaths.h"

#include <iostream>
#include <stdexcept>

namespace astra_mail
{
    MailSendPipeline createPipelineFromThunderbird(); // определена в CmdSendMailsFromPath.cpp

    static void countQueue(const FlashMailScanResult& queue, SendCycleReport& report)
    {
        report.queuedMessages = queue.mails.size();
        report.awaitingRecipients = report.unconfirmedRecipients = 0;
        for (const auto& mail : queue.mails) {
            MailDeliveryState state;
            std::string error;
            if (!state.load(mail.folderPath, error)) {
                if (report.lastError.empty()) report.lastError = error;
                continue;
            }
            if (state.exists) {
                for (const auto& entry : state.recipients) {
                    if (entry.status != RecipientDelivery::Delivered) ++report.awaitingRecipients;
                    if (entry.status == RecipientDelivery::Unknown) ++report.unconfirmedRecipients;
                }
            } else {
                std::string recipients = mail.recipient;
                std::replace(recipients.begin(), recipients.end(), ';', ',');
                std::istringstream input(recipients);
                std::string address;
                std::set<std::string> unique;
                while (std::getline(input, address, ',')) if (!address.empty()) unique.insert(address);
                report.awaitingRecipients += unique.size();
            }
        }
    }

    bool CmdSendMailsFromOutbox::runSendFromOutbox(std::ostream* log, SendCycleReport* output)
    {
        MailOperationScope operation;
        SendCycleReport report;
        auto outboxPath = getOutboxRoot();
        auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());
        MailFolderScanner scanner({true, true, false}, resolver);
        auto queue = scanner.scanMount(outboxPath);
        if (log) MailFolderReport::print(*log, queue);
        auto& tb = ThunderbirdSettings::instance();
        if (!queue.mails.empty() && (!tb.active().valid() || !tb.hasMailPassword())) {
            report.lastError = "Аккаунт не выбран или заблокирован. Введите PIN.";
        } else if (!queue.mails.empty()) {
            MailSendPipeline pipeline = createPipelineFromThunderbird();
            SmtpClient smtp(pipeline.smtpHost(), pipeline.smtpPort());
            smtp.setCredentials(pipeline.smtpUser(), pipeline.smtpPass());
            SmtpClient::Session session;
            for (const auto& mail : queue.mails) {
                if (net::stopping()) break;
                std::string detail;
                MailAttemptResult attempt;
                if (log) *log << "\n[ОТПРАВКА] " << mail.recipient << " ..." << std::flush;
                bool complete = pipeline.send(mail, detail, smtp, session, false, &attempt);
                report.confirmedRecipients += attempt.confirmed;
                if (!attempt.warning.empty()) report.lastError = attempt.warning;
                if (complete) {
                    try { fs::remove_all(mail.folderPath); ++report.completedMessages; }
                    catch (const std::exception& error) { complete = false; detail = error.what(); }
                }
                if (!complete) report.lastError = detail;
                if (log) *log << (complete ? "\r[ОТПРАВЛЕНО] " : "\r[ОСТАЛОСЬ В ИСХОДЯЩИХ] ")
                              << mail.recipient << "\n" << detail << "\n";
                if (attempt.transportFailed) break; // One failed channel, one attempt per cycle.
            }
            SmtpClient::closeSession(session);
        }
        report.cancelled = net::stopping();
        countQueue(scanner.scanMount(outboxPath), report);
        if (log && !report.lastError.empty()) *log << report.lastError << "\n";
        if (output) *output = report;
        return report.queuedMessages == 0 && !report.cancelled;
    }
    CommandResult CmdSendMailsFromOutbox::execute()
    {
        MailOperationScope operation(std::try_to_lock);
        if (!mailCommandAllowed(operation.allowed())) return CommandResult::Continue;
        IdleBackground::BusyGuard busy;
        bool ok = runSendFromOutbox(&std::cout);
        if (!ok)
            std::cout << "Часть писем осталась в исходящих. Причины указаны выше.\n";
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
