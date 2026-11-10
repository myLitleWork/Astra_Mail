#include "menu/MailCommandAccess.h"
#include "CmdResetConfig.h"
#include "Utils/Confirm.h"
#include <iostream>

namespace astra_mail
{
    CmdResetConfig::CmdResetConfig(std::string title, Action action)
            : title_(std::move(title))
            , action_(std::move(action))
    {
    }

    std::string CmdResetConfig::title() const
    {
        return title_;
    }

    CommandResult CmdResetConfig::execute()
    {
        MailDataChangeScope change;
        if (!mailCommandAllowed(change.allowed())) return CommandResult::Continue;
        const std::string warning =
                "ВНИМАНИЕ!\n"
                "Текущие настройки будут УДАЛЕНЫ и заменены значениями по умолчанию.\n"
                "Продолжить?";

        if (!confirm(warning))
        {
            std::cout << "Отменено.\n";
            return CommandResult::Continue;
        }

        action_(true); // force
        std::cout << "[OK] Конфигурация пересоздана\n";

        return CommandResult::Continue;
    }
}
