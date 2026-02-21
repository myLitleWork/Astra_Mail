#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdCleanupOutbox : public ICommand
    {
    public:
        CmdCleanupOutbox();
        std::string title() const override;
        CommandResult execute() override;
    };
}
