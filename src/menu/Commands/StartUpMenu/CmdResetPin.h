#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdResetPin : public ICommand
    {
    public:
        [[nodiscard]] std::string title() const override { return "Сбросить PIN"; }
        CommandResult execute() override;
    };
}
