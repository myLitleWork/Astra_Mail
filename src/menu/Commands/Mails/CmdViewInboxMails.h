#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdViewInboxMails : public ICommand
    {
    public:
        CmdViewInboxMails();
        std::string title() const override;
        CommandResult execute() override;
    };
}
