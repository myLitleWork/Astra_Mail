#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdSendMailsFromPath : public ICommand
    {
        std::string path_;
    public:
        [[nodiscard]] std::string title() const override;


        CommandResult execute() override;

        explicit CmdSendMailsFromPath(const std::string& path);
    };
}
