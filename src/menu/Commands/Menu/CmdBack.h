
#pragma once
#include "menu/Command.h"
#include "menu/Menu.h"

namespace astra_mail {

    class CmdBack : public ICommand  {

        CommandResult execute() override;

        [[nodiscard]] std::string title() const override;


    };

} // astra_mail
