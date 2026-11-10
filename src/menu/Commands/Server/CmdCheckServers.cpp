#include "menu/MailCommandAccess.h"
#include "CmdCheckServers.h"
#include "menu/StartupProfileLoader.h"
#include "Utils/Utils.h"

namespace astra_mail
{
    CommandResult CmdCheckServers::execute()
    {
        MailOperationScope operation(std::try_to_lock);
        if (!mailCommandAllowed(operation.allowed())) return CommandResult::Continue;
        checkServers();
        my::pause();
        return CommandResult::Continue;
    }

    CmdCheckServers::CmdCheckServers() : ICommand() {}

    std::string CmdCheckServers::title() const
    {
        return "Проверка подключения к серверам";
    }
}
