#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdCheckServers : public ICommand
    {
    public:
        CmdCheckServers();
        std::string title() const override;
        CommandResult execute() override;
    };
}
