#include "CmdCleanupGarbageMounts.h"
#include "Utils/FlashDriveScanner.h"
#include "Utils/Utils.h"

#include <iostream>

namespace astra_mail
{
    std::string CmdCleanupGarbageMounts::title() const
    {
        return "Очистить мусорные точки монтирования";
    }

    CommandResult CmdCleanupGarbageMounts::execute()
    {
        clearScreen();

        std::cout << "Очистка мусорных точек монтирования...\n";

        FlashDriveScanner scanner;
        scanner.cleanupGarbageMounts();

        std::cout << "Готово.\n";
        my::pause();

        return CommandResult::Continue;
    }
}
