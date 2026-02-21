//
// Created by Sg on 14.12.2025.
//

#include "CmdOpenMenu.h"
#include "IdleBackground.h"

astra_mail::CommandResult astra_mail::CmdOpenMenu::execute() {

    astra_mail::IdleBackground::instance().resetTimer();

    auto menu = factory_();   // ← КАЖДЫЙ РАЗ НОВОЕ МЕНЮ
    menu->run();
    return CommandResult::Continue;

}

std::string astra_mail::CmdOpenMenu::title() const {
    return title_;
}
