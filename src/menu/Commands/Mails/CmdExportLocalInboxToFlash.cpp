#include "CmdExportLocalInboxToFlash.h"
#include "CmdExportUnreadToFolders.h"
#include "IdleBackground.h"
#include "Utils/Utils.h"
#include <iostream>

namespace astra_mail
{
    CmdExportLocalInboxToFlash::CmdExportLocalInboxToFlash(
            std::string mountPoint, ExportStrategy strategy)
            : ICommand()
            , mountPoint_(std::move(mountPoint))
            , strategy_(strategy)
    {
    }

    std::string CmdExportLocalInboxToFlash::title() const
    {
        std::string mode = (strategy_.mode == ExportMode::BySubject)
                           ? "по темам" : "по типам вложений";
        return "Офлайн-экспорт (" + mode + ") → " + mountPoint_;
    }

    CommandResult CmdExportLocalInboxToFlash::execute()
    {
        IdleBackground::BusyGuard busy;
        bool ok = CmdExportUnreadToFolders::runExportFromLocalInbox(
                mountPoint_, strategy_, &std::cout);
        if (!ok)
            std::cout << "[Ошибка] Не удалось выполнить офлайн-перенос.\n";
        my::pause();
        return CommandResult::ExitMenu;
    }
}
