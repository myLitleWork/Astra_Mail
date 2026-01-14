#include "CmdShowSmtpSettings.h"
#include "ThunderbirdSettings.h"
#include <iostream>
#include "Utils/Utils.h"

namespace astra_mail {

    std::string CmdShowSmtpSettings::title() const
    {
        return "Показать SMTP настройки (Thunderbird)";
    }

    CommandResult CmdShowSmtpSettings::execute()
    {
        auto& s = ThunderbirdSettings::instance();

        for (const auto& kv : s.smtpServers())
        {
            const auto& id = kv.first;
            const auto& sm = kv.second;

            std::cout
                    << "\nSMTP server: " << id
                    << "\n  host: " << sm.host
                    << "\n  port: " << sm.port
                    << "\n  user: " << sm.user
                    << "\n  ssl : " << (sm.ssl ? "yes" : "no")
                    << "\n";
        }

        my::pause();
    }

}
