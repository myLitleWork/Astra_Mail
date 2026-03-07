#include "IdleBackground.h"
#include "IdleSettings.h"
#include "menu/StartupProfileLoader.h"
#include "ThunderbirdSettings.h"
#include "AstraMailPaths.h"
#include "Utils/ImapClient.h"
#include "Utils/ImapUtf7.h"
#include "Utils/ImapFolderFilter.h"
#include "Configs/Confg/ConfigPaths.h"
#include "menu/Commands/Mails/CmdExportUnreadToFolders.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "MailFolderScanner.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "ContactsManager.h"
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

    void IdleBackground::start()
    {
        if (thread_.joinable())
            return;
        stop_ = false;
        lastActivity_ = std::chrono::steady_clock::now();
        thread_ = std::thread(&IdleBackground::threadLoop, this);
    }

    void IdleBackground::stop()
    {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
    }

    void IdleBackground::resetTimer()
    {
        lastActivity_ = std::chrono::steady_clock::now();
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
            IdleSettingsValues settings = loadIdleSettings();
            uint32_t checkSec = (settings.checkIntervalSeconds > 0) ? settings.checkIntervalSeconds : 30u;
            uint32_t idleSec = (settings.idleThresholdSeconds > 0) ? settings.idleThresholdSeconds : 300u;

            for (uint32_t i = 0; i < checkSec && !stop_; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));

            if (stop_)
                break;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity_).count();
            if (static_cast<uint32_t>(elapsed) < idleSec)
                continue;

            if (busyCount_.load(std::memory_order_relaxed) != 0)
                continue;

            runActions();
            resetTimer();
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
        if (!checkServersSilent())
            return;

        size_t inboxBefore = countInboxLetters();
        size_t outboxBefore = countOutboxLetters();
        showBackgroundMenu(inboxBefore, outboxBefore, "Экспорт в inbox и отправка из outbox...");

        auto& tb = ThunderbirdSettings::instance();
        if (!tb.active().valid() || !tb.hasMailPassword())
            return;

        const auto& active = tb.active();
        const auto& imap = tb.imapServers().at(active.imapServerId);
        const std::string& password = tb.mailPassword();

        std::vector<std::string> rawFolders;
        std::string err;
        if (!ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, rawFolders, err))
            return;

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

        if (!imapFolders.empty())
        {
            std::string inboxPath = getInboxRoot().string();
            CmdExportUnreadToFolders::runExport(
                    imap.host, imap.port, imap.user, password,
                    imapFolders, inboxPath,
                    ExportStrategy{ ExportMode::BySubject, false },
                    nullptr
            );
            showBackgroundMenu(countInboxLetters(), countOutboxLetters(), "Экспорт завершён. Отправка из outbox...");
        }

        CmdSendMailsFromOutbox::runSendFromOutbox(nullptr);

        size_t inboxAfter = countInboxLetters();
        size_t outboxAfter = countOutboxLetters();
        showBackgroundMenu(inboxAfter, outboxAfter, "Готово. Нажмите Enter для возврата в меню.");
    }
}
