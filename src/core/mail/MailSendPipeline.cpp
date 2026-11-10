#include "MailSendPipeline.h"
#include "AccountAccess.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/SensitiveString.h"
#include "MailDeliveryState.h"
#include "Resolver/Resolvers/OutgoingSubjectResolver.h"
#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "Utils/EmailMessage.h"
#include "core/mime/MimeTemporaryFile.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>

namespace astra_mail {
namespace {
std::vector<std::string> splitRecipients(const std::string& text) {
    std::vector<std::string> result;
    std::set<std::string> unique;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find_first_of(",;", start);
        std::string value = text.substr(start, end == std::string::npos ? end : end - start);
        const auto first = value.find_first_not_of(" \t\r\n");
        const auto last = value.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            value = value.substr(first, last - first + 1);
            if (unique.insert(value).second) result.push_back(value);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
std::string joinRecipients(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += ",";
        result += value;
    }
    return result;
}
bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
}

MailSendPipeline::MailSendPipeline(const std::string& smtpHost, int smtpPort,
    const std::string& imapHost, int imapPort, const std::string& user, const std::string& pass)
    : smtpHost_(smtpHost), imapHost_(imapHost), smtpPort_(smtpPort), imapPort_(imapPort), user_(user), pass_(pass) {
    MailOperationScope operation;
    accountContext_ = ThunderbirdSettings::instance().secretContext();
}

MailSendPipeline::~MailSendPipeline() { clearSensitiveString(pass_); }

bool MailSendPipeline::send(const MailFolderInfo& info, std::string& log, bool verbose) {
    SmtpClient smtp(smtpHost_, smtpPort_);
    smtp.setCredentials(user_, pass_);
    SmtpClient::Session session;
    const bool ok = send(info, log, smtp, session, verbose);
    SmtpClient::closeSession(session);
    return ok;
}

bool MailSendPipeline::send(const MailFolderInfo& info, std::string& log,
                           SmtpClient& smtp, SmtpClient::Session& session, bool verbose, MailAttemptResult* attempt) {
    MailOperationScope operation;
    if (attempt) *attempt = MailAttemptResult{};
    try {
        log.clear();
        const auto& settings = ThunderbirdSettings::instance();
        if (accountContext_ != settings.secretContext() || !settings.hasMailPassword() || pass_ != settings.mailPassword()) {
            log = "Аккаунт или пароль изменились. Повторите отправку из меню после ввода PIN.";
            return false;
        }
        MailDeliveryLock lock(info.folderPath);
        if (!lock.locked()) { log = "Письмо занято другой отправкой или недоступно"; return false; }
        MailDeliveryState state;
        if (!state.load(info.folderPath, log)) return false;
        std::string fingerprint;
        if (!mailPayloadFingerprint(info, fingerprint, log)) return false;
        const std::string context = smtpHost_ + "\n" + std::to_string(smtpPort_) + "\n" + user_;
        if (state.exists) {
            if (state.context != context || state.fingerprint != fingerprint) {
                log = "Сервер, отправитель или вложения изменились после начала отправки. "
                      "Автоматический повтор запрещён: сохраните письмо отдельно для проверки.";
                return false;
            }
        } else {
            state.context = context; state.fingerprint = fingerprint;
            for (const auto& address : splitRecipients(info.recipient))
                state.recipients.push_back(RecipientProgress{address, RecipientDelivery::Pending});
            if (!state.save(info.folderPath, log)) return false;
        }
        if (state.complete()) { log = state.summary(); return true; }
        const auto recipients = state.pending();
        if (recipients.empty()) {
            log = state.summary() + ". Проверьте результат через меню «Результаты отправки».";
            return false;
        }

        std::vector<std::string> names;
        for (const auto& attachment : info.attachments) names.push_back(attachment.name);
        config::ensureOutgoingSubjectRules(false);
        OutgoingSubjectResolver resolver(config::outgoingSubjectRules());
        resolver.load();
        EmailMessage message;
        message.from = user_; message.to = joinRecipients(recipients);
        message.subject = resolver.resolve(names);
        message.body = "Во вложении документы.";
        for (const auto& attachment : info.attachments) {
            EmailAttachment value;
            value.filename = attachment.name; value.path = attachment.fullPath;
            message.attachments.push_back(std::move(value));
        }
        MimeTemporaryFile mime;
        if (!mime.create(log) || !MimeFileBuilder::buildToStream(message, mime.stream(), log)) return false;
        if (session.sock < 0 && !smtp.openSession(session, log)) {
            if (attempt) attempt->transportFailed = true;
            return false;
        }

        // Durable in-flight marker BEFORE transmission. Without a saved positive
        // reply, the next cycle retries; the user explicitly accepts duplicates.
        std::set<std::string> previouslyUnknown;
        for (const auto& entry : state.recipients)
            if (entry.status == RecipientDelivery::Unknown) previouslyUnknown.insert(entry.address);
        for (auto& entry : state.recipients)
            if (contains(recipients, entry.address)) entry.status = RecipientDelivery::Unknown;
        if (!state.save(info.folderPath, log)) return false;
        if (verbose) std::cout << "[SMTP] Отправка " << recipients.size() << " адресатам...\n";
        const auto result = smtp.deliver(session, mime.stream(), recipients);
        if (attempt) {
            attempt->confirmed = result.delivered.size(); attempt->unconfirmed = result.unknown.size();
            attempt->transportFailed = result.transportFailed;
        }
        for (auto& entry : state.recipients) {
            if (contains(result.delivered, entry.address)) entry.status = RecipientDelivery::Delivered;
            else if (contains(result.pending, entry.address)) entry.status = previouslyUnknown.count(entry.address) ?
                RecipientDelivery::Unknown : RecipientDelivery::Pending;
        }
        if (!state.save(info.folderPath, log)) {
            log += ". Итог не записан: при следующей попытке возможен дубль.";
            return false;
        }
        log = state.summary() + ". " + result.detail;
        if (!result.delivered.empty()) {
            // Delivery progress is committed before the optional Sent copy.
            ImapClient imap(false);
            std::string error;
            if (!imap.imapAppendSentFromStream(mime.stream(), error)) {
                if (attempt) attempt->warning = "Копия в Отправленные не сохранена: " + error;
                log += ". Копия в Отправленные не сохранена: " + error;
                std::cout << "\n[ВНИМАНИЕ] SMTP принял письмо для " << result.delivered.size()
                          << " адресатов, но копия в Отправленные не сохранена: " << error << "\n";
            }
        }
        return state.complete();
    } catch (const std::exception& error) {
        log = std::string("Отправка остановлена: ") + error.what();
        // Durable in-flight entries remain Unknown and are retried next cycle.
        SmtpClient::closeSession(session);
        return false;
    }
}
}
