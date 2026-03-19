#include "CmdShowLocalProfileSettings.h"

#include "core/profile/LocalProfileCache.h"
#include "Utils/Utils.h"

#include <iostream>
#include <sstream>

namespace astra_mail
{
    std::string CmdShowLocalProfileSettings::title() const
    {
        return "Показать текущие настройки (локальные)";
    }

    CommandResult CmdShowLocalProfileSettings::execute()
    {
        LocalMailSettings s;
        if (!loadLocalMailSettings(s))
        {
            std::cout << "Локальные настройки не найдены.\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::string reason;
        const bool ok = validateLocalMailSettings(s, reason);

        std::cout << "=== Локальные настройки профиля ===\n";
        std::cout << "Профиль: " << (s.profileId.empty() ? "<неизвестно>" : s.profileId) << "\n";
        std::cout << "Путь   : " << (s.profilePath.empty() ? "<неизвестно>" : s.profilePath) << "\n";
        std::cout << "Ящик   : " << (s.fullName.empty() ? "" : (s.fullName + " "))
                  << "<" << s.email << ">\n";
        std::cout << "Логин  : " << s.userName << "\n";
        std::cout << "IMAP   : " << s.imapHost << ":" << s.imapPort
                  << " SSL=" << (s.imapSsl ? "yes" : "no") << "\n";
        std::cout << "SMTP   : " << s.smtpHost << ":" << s.smtpPort
                  << " SSL=" << (s.smtpSsl ? "yes" : "no") << "\n";
        std::cout << "Статус : " << (ok ? "OK" : ("НЕВАЛИДНО: " + reason)) << "\n";

        my::pause();
        return CommandResult::Continue;
    }
}

