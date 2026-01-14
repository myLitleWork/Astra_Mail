#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdEnterPin : public ICommand
    {
    public:
        [[nodiscard]] std::string title() const override { return "Ввести PIN"; }
        CommandResult execute() override;
    };
}
