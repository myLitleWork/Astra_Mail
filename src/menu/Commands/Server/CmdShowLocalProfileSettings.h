#pragma once

#include "menu/Command.h"

namespace astra_mail
{
    class CmdShowLocalProfileSettings final : public ICommand
    {
    public:
        std::string title() const override;
        CommandResult execute() override;
    };
}

