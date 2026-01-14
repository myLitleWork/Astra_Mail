#include "CmdShowImapSettings.h"
#include "ThunderbirdSettings.h"
#include <iostream>
#include "Utils/Utils.h"

namespace astra_mail {

    std::string CmdShowImapSettings::title() const
    {
        return "Показать IMAP настройки (Thunderbird)";
    }

    CommandResult CmdShowImapSettings::execute()
    {
        auto& s = ThunderbirdSettings::instance();

        for (const auto& kv: s.imapServers())
        {

            const auto& id = kv.first;
            const auto& im = kv.second;

            std::cout
                    << "\nIMAP server: " << id
                    << "\n  host: " << im.host
                    << "\n  port: " << im.port
                    << "\n  user: " << im.user
                    << "\n  ssl : " << (im.ssl ? "yes" : "no")
                    << "\n";
        }

        my::pause();
    }

}
