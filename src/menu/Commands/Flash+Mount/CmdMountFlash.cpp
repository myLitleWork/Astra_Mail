#include "CmdMountFlash.h"
#include "src/Utils/FlashDriveScanner.h"
#include "src/Utils/Utils.h"

#include <iostream>
#include <cstdlib>

namespace astra_mail
{
    CmdMountFlash::CmdMountFlash(const FlashDriveInfo& drive)
            : drive_(drive)
    {
    }

    std::string CmdMountFlash::title() const
    {
        return drive_.label + " (смонтировать)";
    }

    CommandResult CmdMountFlash::execute()
    {
        std::cout << "Монтирование " << drive_.device << "...\n";

        // стандартный способ для Astra / systemd
        std::string cmd = "udisksctl mount -b " + drive_.device + " >/dev/null 2>&1";
        int rc = std::system(cmd.c_str());

        if (rc != 0)
        {
            std::cout << "Ошибка монтирования\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "Флешка смонтирована\n";
        my::pause();

        // ВАЖНО:
        // закрываем меню → фабрика пересоберёт его
        return CommandResult::ExitMenu;
    }
}
