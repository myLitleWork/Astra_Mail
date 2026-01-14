#include "StartupServerCheck.h"

#include "ThunderbirdSettings.h"
#include "Utils/SmtpClient.h"

#include <iostream>

namespace astra_mail
{

    bool checkMailServers()
    {
        auto& tb = ThunderbirdSettings::instance();

        if (!tb.active().valid() || !tb.hasMailPassword())
        {
            std::cout << "Нет активной конфигурации или пароля\n";
            return false;
        }

        const auto& im = tb.imapServers().at(tb.active().imapServerId);
        const auto& sm = tb.smtpServers().at(tb.active().smtpServerId);

        const std::string& password = tb.mailPassword();

        return true;
    }

} // namespace astra_mail
