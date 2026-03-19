#pragma once

#include "menu/Command.h"

namespace astra_mail
{
    class CmdEditLocalProfileSettings final : public ICommand
    {
    public:
        std::string title() const override;
        CommandResult execute() override;
    };
}

