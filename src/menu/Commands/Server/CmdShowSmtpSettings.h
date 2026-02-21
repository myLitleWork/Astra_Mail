#pragma once
#include "menu/Command.h"

namespace astra_mail {

    class CmdShowSmtpSettings : public ICommand {
    public:
        CmdShowSmtpSettings() = default;

        std::string title() const override;
        CommandResult execute() override;
    };

}
