#include "IdleBackground.h"
#include "core/mail/AccountAccess.h"
#include "IdleSettings.h"
#include "menu/StartupProfileLoader.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/paths/AstraMailPaths.h"
#include "Utils/ImapClient.h"
#include "Utils/ImapUtf7.h"
#include "Utils/ImapFolderFilter.h"
#include "Configs/Confg/ConfigPaths.h"
#include "menu/Commands/Mails/CmdExportUnreadToFolders.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "core/mail/MailFolderScanner.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "core/contacts/ContactsManager.h"
#include "UtilsFS/FsCompat.h"

#include <thread>
#include <chrono>
#include <iostream>

namespace astra_mail
{
    IdleBackground& IdleBackground::instance()
    {
        static IdleBackground inst;
        return inst;
    }

    static int64_t activityNowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    IdleBackground::~IdleBackground() { stop(); }

    void IdleBackground::start()
    {
        if (thread_.joinable())
            return;
        net::resetStop();
        stop_ = false;
        lastActivityMs_.store(activityNowMs());
        thread_ = std::thread(&IdleBackground::threadLoop, this);
    }

    void IdleBackground::stop()
    {
        stop_ = true;
        net::requestStop();
        if (thread_.joinable())
            thread_.join();
    }

    void IdleBackground::resetTimer()
    {
        lastActivityMs_.store(activityNowMs());
    }

    void IdleBackground::enterBusy()
    {
        busyCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void IdleBackground::leaveBusy()
    {
        busyCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    void IdleBackground::threadLoop()
    {
        while (!stop_)
        {
            // Also bound the retry rate when configuration/filesystem operations throw.
            for (unsigned i = 0; i < 10 && !stop_; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop_) break;
            try {
                IdleSettingsValues settings;
                {
                    MailOperationScope configRead(std::try_to_lock);
                    if (!configRead.allowed() || busyCount_.load() != 0) continue;
                    settings = loadIdleSettings();
                }
                const uint32_t checkSec = settings.checkIntervalSeconds ? settings.checkIntervalSeconds : 30u;
                const uint32_t idleSec = settings.idleThresholdSeconds ? settings.idleThresholdSeconds : 300u;
                for (uint64_t i = 10; i < uint64_t(checkSec) * 10 && !stop_; ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (stop_) break;
                const int64_t elapsed = activityNowMs() - lastActivityMs_.load();
                if (elapsed < int64_t(idleSec) * 1000 || busyCount_.load() != 0) continue;
                runActions();
                resetTimer();
            } catch (const std::exception& error) {
                resetTimer();
                std::lock_guard<std::mutex> lock(coutMutex_);
                std::cout << "\nФоновый цикл прерван: " << error.what()
                          << ". Повтор после следующего периода бездействия.\n" << std::flush;
            } catch (...) {
                resetTimer();
                std::lock_guard<std::mutex> lock(coutMutex_);
                std::cout << "\nФоновый цикл прерван. Повтор после следующего периода бездействия.\n" << std::flush;
            }
        }
    }
    size_t IdleBackground::countInboxLetters() const
    {
        size_t count = 0;
        fs::path inboxRoot = getInboxRoot();
        std::error_code ec;
        if (!fs::exists(inboxRoot, ec))
            return 0;
        for (const auto& subjEntry : fs::directory_iterator(inboxRoot, ec))
        {
            if (ec || !fs::is_directory(subjEntry))
                continue;
            for (const auto& uidEntry : fs::directory_iterator(subjEntry.path(), ec))
            {
                if (ec || !fs::is_directory(uidEntry))
                    continue;
                std::string name = uidEntry.path().filename().string();
                if (name.size() > 4 && name.compare(0, 4, "uid_") == 0)
                    ++count;
            }
        }
        return count;
    }

    size_t IdleBackground::countOutboxLetters() const
    {
        fs::path outboxRoot = getOutboxRoot();
        if (!fs::exists(outboxRoot))
            return 0;
        auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());
        MailFolderScanner scanner({true, true, false}, resolver);
        auto result = scanner.scanMount(outboxRoot);
        return result.mails.size();
    }

    void IdleBackground::showBackgroundMenu(size_t inboxCount, size_t outboxCount, const std::string& status) const
    {
        std::lock_guard<std::mutex> lock(coutMutex_);
        std::cout << "\033[2J\033[H";  // clear screen, cursor home
        std::cout << "====== Фоновый режим (бездействие) ======\n\n";
        std::cout << "  Скачано в inbox   : " << inboxCount << " писем\n";
        std::cout << "  Ожидает отправки  : " << outboxCount << " писем\n\n";
        std::cout << "  " << status << "\n";
        std::cout.flush();
    }

    void IdleBackground::runActions()
    {
        MailOperationScope operation(std::try_to_lock);
        if (!operation.allowed() || busyCount_.load() != 0 || stop_) return;
        size_t inboxBefore = countInboxLetters();
        size_t outboxBefore = countOutboxLetters();

        auto& tb = ThunderbirdSettings::instance();
        if (!tb.active().valid() || !tb.hasMailPassword())
            return;

        const auto& active = tb.active();
        const auto& imap = tb.imapServers().at(active.imapServerId);
        const std::string& password = tb.mailPassword();

        showBackgroundMenu(inboxBefore, outboxBefore, "Проверка IMAP и исходящей очереди...");

        // --- Экспорт входящих (IMAP) — пробуем, но не блокируем отправку ---
        bool imapOk = false;
        std::string imapError;
        {
            std::vector<std::string> rawFolders;
            std::string& err = imapError;
            if (ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, rawFolders, err))
            {
                imapOk = true;
                std::vector<ImapFolder> imapFolders;
                for (const std::string& raw : rawFolders)
                {
                    std::string display = imap::decodeUtf7(raw);
                    if (display.empty())
                        continue;
                    imapFolders.emplace_back(raw, display);
                }

                imap::FolderFilter filter(config::imapFoldersConfig());
                filter.load();
                for (auto it = imapFolders.begin(); it != imapFolders.end(); )
                {
                    if (!filter.shouldShow(it->displayName))
                        it = imapFolders.erase(it);
                    else
                        ++it;
                }

                if (!imapFolders.empty() && !net::stopping())
                {
                    showBackgroundMenu(inboxBefore, outboxBefore, "Экспорт входящих...");
                    std::string inboxPath = getInboxRoot().string();
                    CmdExportUnreadToFolders::runExport(
                            imap.host, imap.port, imap.user, password,
                            imapFolders, inboxPath,
                            ExportStrategy{ ExportMode::BySubject, false },
                            nullptr
                    );
                }
            }
        }

        // An IMAP failure does not prevent an SMTP attempt. Shutdown cancels both.
        if (net::stopping()) return;
        const size_t outboxNow = countOutboxLetters();
        showBackgroundMenu(countInboxLetters(), outboxNow,
                           imapOk ? "Отправка из outbox..." : "IMAP недоступен. Пробуем отправить outbox...");
        SendCycleReport report;
        CmdSendMailsFromOutbox::runSendFromOutbox(nullptr, &report);
        if (net::stopping()) return;
        const auto settings = loadIdleSettings();
        std::string status = report.queuedMessages ? "Часть писем ожидает отправки." : "Исходящая очередь обработана.";
        status += "\n  Подтверждено сервером за цикл: " + std::to_string(report.confirmedRecipients) + " адресатов";
        status += "\n  Полностью отправлено за цикл: " + std::to_string(report.completedMessages) + " писем";
        status += "\n  Ожидают подтверждения: " + std::to_string(report.awaitingRecipients) + " адресатов";
        status += "\n  Из них результат неизвестен: " + std::to_string(report.unconfirmedRecipients);
        if (report.unconfirmedRecipients) status += " (будет автоповтор, возможен дубль)";
        if (!report.lastError.empty()) status += "\n  Последняя ошибка: " + report.lastError;
        if (!imapOk) status += "\n  Входящие не проверены: " + imapError;
        status += "\n  Следующий цикл после " + std::to_string(settings.idleThresholdSeconds ? settings.idleThresholdSeconds : 300u) +
                  " с бездействия (проверка каждые " + std::to_string(settings.checkIntervalSeconds ? settings.checkIntervalSeconds : 30u) + " с).";
        showBackgroundMenu(countInboxLetters(), report.queuedMessages, status);
    }
}
