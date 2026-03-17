#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdExportUnreadToInbox : public ICommand
    {
    public:
        CmdExportUnreadToInbox();
        std::string title() const override;
        CommandResult execute() override;
    };
}
