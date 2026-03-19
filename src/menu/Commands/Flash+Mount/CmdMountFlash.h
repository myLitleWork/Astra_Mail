#pragma once
#include "menu/Command.h"
#include "Utils/FlashDriveScanner.h"


namespace astra_mail
{

    class CmdMountFlash : public ICommand
    {
    public:
        explicit CmdMountFlash(const FlashDriveInfo& drive);

        std::string title() const override;
        CommandResult execute() override;

    private:
        FlashDriveInfo drive_;
    };
}
