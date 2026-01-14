#include "CmdEditConfig.h"
#include "Configs/Editor/EditorLauncher.h"
#include <iostream>

namespace astra_mail
{
    CmdEditConfig::CmdEditConfig(ConfigItem item)
            : item_(std::move(item))
    {
    }

    std::string CmdEditConfig::title() const
    {
        return item_.title;
    }

    CommandResult CmdEditConfig::execute()
    {
        std::string err;

        if (!system::openInEditor(item_.path, err))
            std::cout << "[ERROR] " << err << "\n";

        return CommandResult::Continue;
    }
}
