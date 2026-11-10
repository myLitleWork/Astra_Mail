#include "menu/MailCommandAccess.h"
#include "CmdCleanupOutbox.h"
#include "core/paths/AstraMailPaths.h"
#include "Utils/Confirm.h"
#include "Utils/Utils.h"
#include "UtilsFS/FsCompat.h"
#include <iostream>

namespace astra_mail
{
    CommandResult CmdCleanupOutbox::execute()
    {
        MailDataChangeScope change;
        if (!mailCommandAllowed(change.allowed())) return CommandResult::Continue;
        const std::string warning =
                "ВНИМАНИЕ!\n"
                "Будут безвозвратно удалены ВСЕ письма из папки исходящих.\n"
                "Письма, ожидающие отправки, будут потеряны.\n"
                "Продолжить?";

        if (!confirm(warning))
        {
            std::cout << "Отменено.\n";
            my::pause();
            return CommandResult::Continue;
        }

        fs::path root = getOutboxRoot();
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            std::cout << "Папка исходящих пуста.\n";
            my::pause();
            return CommandResult::Continue;
        }

        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            fs::remove_all(it->path(), ec);
        }

        std::cout << "[OK] Папка исходящих очищена.\n";
        my::pause();
        return CommandResult::Continue;
    }

    CmdCleanupOutbox::CmdCleanupOutbox() : ICommand() {}

    std::string CmdCleanupOutbox::title() const
    {
        return "Очистить исходящие";
    }
}
