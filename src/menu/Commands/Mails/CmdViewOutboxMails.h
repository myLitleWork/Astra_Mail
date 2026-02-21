#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdViewOutboxMails : public ICommand
    {
    public:
        CmdViewOutboxMails();
        std::string title() const override;
        CommandResult execute() override;
    };
}
