#pragma once
#include "menu/Command.h"
#include "src/Utils/FlashDriveScanner.h"
#include <string>

namespace astra_mail
{
    class CmdUmountFlash : public ICommand
    {
    public:
        explicit CmdUmountFlash(const FlashDriveInfo& drive);

        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;

    private:
        FlashDriveInfo drive_;
    };
}
