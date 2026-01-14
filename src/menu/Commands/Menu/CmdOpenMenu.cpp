//
// Created by Sg on 14.12.2025.
//

#include "CmdOpenMenu.h"

astra_mail::CommandResult astra_mail::CmdOpenMenu::execute() {

    auto menu = factory_();   // ← КАЖДЫЙ РАЗ НОВОЕ МЕНЮ
    menu->run();
    return CommandResult::Continue;

}

std::string astra_mail::CmdOpenMenu::title() const {
    return title_;
}
