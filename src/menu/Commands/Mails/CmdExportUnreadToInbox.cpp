#include "CmdExportUnreadToInbox.h"
#include "core/idle/IdleBackground.h"
#include "CmdExportUnreadToFolders.h"
#include "core/paths/AstraMailPaths.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/ImapClient.h"
#include "Utils/ImapUtf7.h"
#include "Utils/ImapFolderFilter.h"
#include "Utils/Utils.h"
#include "Configs/Confg/ConfigPaths.h"
#include <iostream>

namespace astra_mail
{
    CommandResult CmdExportUnreadToInbox::execute()
    {
        IdleBackground::BusyGuard busy;
        auto& tb = ThunderbirdSettings::instance();
        if (!tb.active().valid())
        {
            std::cout << "[Ошибка] Почтовый аккаунт не выбран.\n";
            my::pause();
            return CommandResult::Continue;
        }
        if (!tb.hasMailPassword())
        {
            std::cout << "[Ошибка] Пароль почты не задан. Введите PIN в меню безопасности.\n";
            my::pause();
            return CommandResult::Continue;
        }

        const auto& active = tb.active();
        const auto& imap = tb.imapServers().at(active.imapServerId);
        const std::string& password = tb.mailPassword();

        std::vector<std::string> rawFolders;
        std::string err;
        if (!ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, rawFolders, err))
        {
            std::cout << "[Ошибка] IMAP недоступен: " << err << "\n";
            my::pause();
            return CommandResult::Continue;
        }

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

        if (imapFolders.empty())
        {
            std::cout << "[Ошибка] Нет папок для экспорта.\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::string inboxPath = getInboxRoot().string();
        bool ok = CmdExportUnreadToFolders::runExport(
                imap.host, imap.port, imap.user, password,
                imapFolders, inboxPath,
                ExportStrategy{ ExportMode::BySubject, false },
                &std::cout
        );

        my::pause();
        return CommandResult::Continue;
    }

    CmdExportUnreadToInbox::CmdExportUnreadToInbox() : ICommand() {}

    std::string CmdExportUnreadToInbox::title() const
    {
        return "Сохранить непрочитанные в локальную папку";
    }
}
