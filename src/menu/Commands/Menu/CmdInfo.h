#pragma once
#include "menu/Command.h"

#include <string>

namespace astra_mail
{
    class CmdInfo : public ICommand
    {
    public:
        CmdInfo(std::string title, std::string message);

        std::string title() const override;
        CommandResult execute() override;



    private:
        std::string message_;
        std::string title_;
    };
}
