#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdEnterPin : public ICommand
    {
    public:
        std::string title() const override { return "Ввести PIN"; }
        CommandResult execute() override;
    };
}
