//
// Created by Sg on 14.12.2025.
//
#include "menu/Command.h"
#include "CmdBack.h"

namespace astra_mail {

    CommandResult CmdBack::execute() {
        return CommandResult::ExitMenu;
    }

    std::string CmdBack::title() const {
        { return "Назад"; }
    }
} // astra_mail