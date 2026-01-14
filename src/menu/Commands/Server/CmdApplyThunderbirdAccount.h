#pragma once

#include "menu/Command.h"
#include "ThunderbirdSettings.h"

namespace astra_mail
{

    class CmdApplyThunderbirdAccount : public ICommand
    {
    public:
        explicit CmdApplyThunderbirdAccount(AccountConfig  acc, TbProfile  profile);

        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;

    private:
        AccountConfig acc_;
        TbProfile profile_;
    };

} // namespace astra_mail
