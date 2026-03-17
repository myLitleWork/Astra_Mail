#pragma once
#include "menu/Command.h"
#include "menu/AttachmentExporter.h"
#include <string>

namespace astra_mail
{
    class CmdExportLocalInboxToFlash : public ICommand
    {
    public:
        CmdExportLocalInboxToFlash(std::string mountPoint, ExportStrategy strategy);

        std::string title() const override;
        CommandResult execute() override;

    private:
        std::string mountPoint_;
        ExportStrategy strategy_;
    };
}
