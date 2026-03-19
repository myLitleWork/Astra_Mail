#include "CmdUmountFlash.h"
#include "Utils/Utils.h"

#include <iostream>
#include <cstdlib>

namespace astra_mail
{
    CmdUmountFlash::CmdUmountFlash(const FlashDriveInfo&  drive)
            : drive_(drive)
    {
    }

    std::string CmdUmountFlash::title() const
    {
        if (drive_.mounted)
            return "Отмонтировать " + drive_.label;
        return "Отмонтировать (не смонтирована)";
    }

    CommandResult CmdUmountFlash::execute()
    {
        clearScreen();

        if (!drive_.mounted || drive_.mountPoint.empty())
        {
            std::cout << "Флешка не смонтирована.\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "Отмонтирование: " << drive_.label << "\n";

        std::string cmd = "umount \"" + drive_.device + "\"";
        int rc = std::system(cmd.c_str());

        if (rc == 0)
        {
            std::cout << "Флешка успешно отмонтирована.\n";
        }
        else
        {
            std::cout << "Ошибка отмонтирования.\n";
            std::cout << "Возможно, каталог занят или недостаточно прав.\n";
        }

        my::pause();
        return CommandResult::ExitMenu;
    }
}
