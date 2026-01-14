#include "CmdApplyThunderbirdAccount.h"
#include "Utils/Utils.h"
#include "ContactsManager.h"
#include "ActiveProfileStorage.h"

#include <iostream>
#include <sstream>
#include <utility>

namespace astra_mail
{

    CmdApplyThunderbirdAccount::CmdApplyThunderbirdAccount(AccountConfig  acc, TbProfile  profile)
            : acc_(std::move(acc)), profile_(std::move(profile))
    {
    }

    std::string CmdApplyThunderbirdAccount::title() const
    {
        auto& tb = ThunderbirdSettings::instance();

        auto yesno = [](bool v){ return v ? "да" : "нет"; };

        // --- identity (email/fullname + smtpServerId) ---
        std::string identityId;
        if (!acc_.identities.empty())
            identityId = acc_.identities.front();

        std::string displayName = "<нет email>";
        std::string smtpId;

        if (!identityId.empty())
        {
            const auto& ids = tb.identities();
            auto itId = ids.find(identityId);
            if (itId != ids.end())
            {
                const auto& id = itId->second;

                if (!id.email.empty())
                {
                    if (!id.fullName.empty())
                        displayName = id.fullName + " <" + id.email + ">";
                    else
                        displayName = id.email;
                }
                smtpId = id.smtpServerId;
            }
        }

        // --- IMAP server (по acc_.serverId) ---
        std::string imapLine = "    IMAP: <нет данных>";
        {
            const auto& imaps = tb.imapServers();
            auto itIm = imaps.find(acc_.serverId);
            if (itIm != imaps.end())
            {
                const auto& im = itIm->second;
                std::ostringstream s;
                s << "    IMAP: " << im.host << ":" << im.port
                  << "  SSL: " << yesno(im.ssl)
                  << "   Логин: " << im.user;
                imapLine = s.str();
            }
        }

        // --- SMTP server (по smtpId из identity) ---
        std::string smtpLine = "    SMTP: <нет данных>";
        if (!smtpId.empty())
        {
            const auto& smtps = tb.smtpServers();
            auto itSm = smtps.find(smtpId);
            if (itSm != smtps.end())
            {
                const auto& sm = itSm->second;
                std::ostringstream s;
                s << "    SMTP: " << sm.host << ":" << sm.port
                  << "  SSL: " << yesno(sm.ssl)
                  << "   Логин: " << sm.user;
                smtpLine = s.str();
            }
        }

        std::ostringstream out;
        out << "Аккаунт: " << displayName << "\n"
            << imapLine << "\n"
            << smtpLine;

        return out.str();
    }


    CommandResult CmdApplyThunderbirdAccount::execute()
    {
        auto& tb = ThunderbirdSettings::instance();

        profile_.active_account_id = acc_.accountId;

        if (!tb.applyAccount(acc_.accountId))
        {
            std::cout << "Не удалось применить аккаунт\n";
            my::pause();
            return CommandResult::Continue;
        }
        else
            std::cout << "Применен аккаунт " << acc_.accountId << "\n";

        bool hasDub = false;
        auto& cm = ContactsManager::instance();
        tb.setCurrentProfile(profile_);
        if (!cm.loadForProfile(tb.currentProfile(), hasDub))
        {
            std::cout << "Аккаунт применён, но контакты не загружены\n";
            my::pause();
            return CommandResult::Continue;
        }

        if (hasDub)
        {
            std::cout << "ВНИМАНИЕ: найдены одинаковые никнеймы контактов\n";
        }

        if (!saveActiveProfile(profile_))
        {
            std::cout << "ОШИБКА: не удалось сохранить активный профиль\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "Аккаунт применён и профиль сохранён\n";
        my::pause();
        return CommandResult::ExitMenu;

    }

} // namespace astra_mail
