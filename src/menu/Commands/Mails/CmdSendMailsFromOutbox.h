#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdSendMailsFromOutbox : public ICommand
    {
    public:
        CmdSendMailsFromOutbox();
        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;
    };
}
