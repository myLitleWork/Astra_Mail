#pragma once
#include "menu/Command.h"

namespace astra_mail
{
    class CmdCleanupGarbageMounts : public ICommand
    {
    public:
        CmdCleanupGarbageMounts() = default;

        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;
    };
}
