#pragma once
#include "menu/Command.h"

namespace astra_mail {
class CmdReviewDelivery : public ICommand {
public:
    std::string title() const override { return "Результаты отправки и адресаты"; }
    CommandResult execute() override;
};
}
