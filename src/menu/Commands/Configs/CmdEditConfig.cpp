#include "CmdEditConfig.h"
#include "Configs/Editor/EditorLauncher.h"
#include "core/idle/IdleBackground.h"
#include <iostream>

namespace astra_mail
{
    CmdEditConfig::CmdEditConfig(ConfigItem item)
            : item_(std::move(item))
    {
    }

    std::string CmdEditConfig::title() const
    {
        return item_.title;
    }

    CommandResult CmdEditConfig::execute()
    {
        // Внешний редактор блокирует текущий поток: пока он запущен — запрещаем idle-действия.
        IdleBackground::BusyGuard busy;

        std::string err;
        bool ok = system::openInEditor(item_.path, err);
        if (!ok)
            std::cout << "[ERROR] " << err << "\n";

        // После закрытия редактора сбрасываем таймер активности.
        IdleBackground::instance().resetTimer();

        return CommandResult::Continue;
    }
}
