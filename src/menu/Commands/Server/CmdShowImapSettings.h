#pragma once
#include "menu/Command.h"

namespace astra_mail {

    class CmdShowImapSettings : public ICommand {
    public:
        CmdShowImapSettings() = default;

        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;
    };

}
