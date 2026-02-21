#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdCleanupInbox : public ICommand
    {
    public:
        CmdCleanupInbox();
        std::string title() const override;
        CommandResult execute() override;
    };
}
