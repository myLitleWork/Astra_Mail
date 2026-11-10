#include "CmdUnlockAccount.h"
#include "menu/StartupProfileLoader.h"
namespace astra_mail {
CommandResult CmdUnlockAccount::execute() {
    pinMenu();
    return CommandResult::Continue;
}
}
