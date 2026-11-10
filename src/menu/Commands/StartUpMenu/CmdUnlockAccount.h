#pragma once
#include "menu/Command.h"
namespace astra_mail {
class CmdUnlockAccount : public ICommand {
public:
    std::string title() const override { return "Разблокировать текущий почтовый аккаунт"; }
    CommandResult execute() override;
};
}
