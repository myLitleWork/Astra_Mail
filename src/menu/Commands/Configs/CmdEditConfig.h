#pragma once
#include "menu/Command.h"
#include "ConfigItem.h"

namespace astra_mail
{
    class CmdEditConfig final : public ICommand
    {
    public:
        explicit CmdEditConfig(ConfigItem item);

        std::string title() const override;
        CommandResult execute() override;

    private:
        ConfigItem item_;
    };
}
