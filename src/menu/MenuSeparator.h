#pragma once
#include "menu/Command.h"

#include <string>

namespace astra_mail {

    class MenuSeparator : public ICommand {
    public:
        [[nodiscard]] inline std::string title() const override {
            return "------------------------------";
        }
        CommandResult execute() override {return CommandResult::Continue;}
    };


} // astra_mail
