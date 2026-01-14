#pragma once
#include "menu/Command.h"
#include <string>
#include <functional>

namespace astra_mail
{
    class CmdResetConfig final : public ICommand
    {
    public:
        using Action = std::function<void(bool)>;

        CmdResetConfig(std::string title, Action action);

        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;

    private:
        std::string title_;
        Action action_;
    };
}
