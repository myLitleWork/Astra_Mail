#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdViewFlashMails : public ICommand
    {
        std::string path_;
    public:
        explicit CmdViewFlashMails(const std::string&  path);
        std::string title() const override;
        CommandResult execute() override;
    };
}
