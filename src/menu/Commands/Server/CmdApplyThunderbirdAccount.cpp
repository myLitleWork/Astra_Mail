#include "Utils/ConsoleInput.h"
#include "CmdApplyThunderbirdAccount.h"
#include "core/mail/AccountAccess.h"
#include "core/idle/IdleBackground.h"
#include "menu/StartupProfileLoader.h"
#include "Utils/Utils.h"
#include "core/contacts/ContactsManager.h"
#include "core/profile/ActiveProfileStorage.h"
#include "core/profile/LocalProfileCache.h"

#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace astra_mail
{
    static std::string readLine(const std::string& prompt)
    {
        if (!std::cin.good()) return {};
        std::cout << prompt;
        std::cout.flush();
        std::string s;
        readConsoleLine(s);
        return s;
    }

    static bool readBoolDefault(const std::string& prompt, bool def)
    {
        const std::string hint = def ? " [Y/n]: " : " [y/N]: ";
        std::string s = readLine(prompt + hint);
        if (s.empty())
            return def;
        const char c = s[0];
        return c == 'y' || c == 'Y';
    }

    static int readIntDefault(const std::string& prompt, int def)
    {
        std::ostringstream p;
        p << prompt << " (" << def << "): ";
        std::string s = readLine(p.str());
        if (s.empty())
            return def;
        try
        {
            return std::stoi(s);
        }
        catch (...)
        {
            return def;
        }
    }

    static void fillMissingSettingsInteractive(LocalMailSettings& s)
    {
        std::cout << "\n[IMPORT] Не все настройки удалось извлечь автоматически.\n"
                  << "Введите недостающие значения (Enter = оставить как есть).\n\n";

        if (s.email.empty())
            s.email = readLine("Email: ");
        if (s.fullName.empty())
            s.fullName = readLine("Имя (опционально): ");
        if (s.userName.empty())
            s.userName = readLine("Логин (user_name): ");

        if (s.imapHost.empty())
            s.imapHost = readLine("IMAP host: ");
        s.imapPort = readIntDefault("IMAP port", s.imapPort);
        s.imapSsl = readBoolDefault("IMAP SSL?", s.imapSsl);

        if (s.smtpHost.empty())
            s.smtpHost = readLine("SMTP host: ");
        s.smtpPort = readIntDefault("SMTP port", s.smtpPort);
        s.smtpSsl = readBoolDefault("SMTP SSL?", s.smtpSsl);
    }


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
        AccountChangeScope change;
        if (!change.allowed()) {
            std::cout << "Переключение аккаунта запрещено до завершения почтовой операции.\n";
            return CommandResult::Continue;
        }
        IdleBackground::BusyGuard busy;
        auto& tb = ThunderbirdSettings::instance();

        profile_.active_account_id = acc_.accountId;

        const bool accountApplied = tb.applyAccount(acc_.accountId);
        if (accountApplied)
            std::cout << "Применен аккаунт " << acc_.accountId << "\n";
        else
            std::cout << "Не удалось применить аккаунт из Thunderbird, перейдем к ручному вводу\n";

        bool hasDub = false;
        auto& cm = ContactsManager::instance();
        tb.setCurrentProfile(profile_);
        if (!cm.loadForProfile(tb.currentProfile(), hasDub))
        {
            std::cout << "Контакты не загружены (можно продолжить без них)\n";
        }

        if (hasDub)
        {
            std::cout << "ВНИМАНИЕ: найдены одинаковые никнеймы контактов\n";
        }

        // --- сохраняем "снимок" профиля в локальные файлы (дальше работаем по ним) ---
        {
            LocalMailSettings s;
            s.profileId = profile_.id;
            s.profilePath = profile_.path;
            s.accountId = acc_.accountId.empty() ? "local_account_1" : acc_.accountId;

            // identity
            std::string identityId = accountApplied ? tb.active().identityId : std::string{};
            if (identityId.empty() && !acc_.identities.empty())
                identityId = acc_.identities.front();
            s.identityId = identityId.empty() ? "local_identity_1" : identityId;

            const auto& ids = tb.identities();
            auto itId = ids.find(s.identityId);
            if (itId != ids.end())
            {
                s.email = itId->second.email;
                s.fullName = itId->second.fullName;
            }

            // IMAP
            std::string imapId = accountApplied ? tb.active().imapServerId : acc_.serverId;
            const auto& imaps = tb.imapServers();
            auto itIm = imaps.find(imapId);
            if (itIm != imaps.end())
            {
                s.imapHost = itIm->second.host;
                s.imapPort = itIm->second.port;
                s.imapSsl = itIm->second.ssl;
                s.userName = itIm->second.user;
            }

            // SMTP
            std::string smtpId;
            if (accountApplied)
                smtpId = tb.active().smtpServerId;
            else if (itId != ids.end())
                smtpId = itId->second.smtpServerId;

            const auto& smtps = tb.smtpServers();
            auto itSm = smtps.find(smtpId);
            if (itSm != smtps.end())
            {
                s.smtpHost = itSm->second.host;
                s.smtpPort = itSm->second.port;
                s.smtpSsl = itSm->second.ssl;
                if (s.userName.empty())
                    s.userName = itSm->second.user;
            }
            else if (!smtps.empty())
            {
                const auto& sm = smtps.begin()->second;
                s.smtpHost = sm.host;
                s.smtpPort = sm.port;
                s.smtpSsl = sm.ssl;
                if (s.userName.empty())
                    s.userName = sm.user;
            }

            std::string reason;
            if (!validateLocalMailSettings(s, reason))
            {
                fillMissingSettingsInteractive(s);
                if (!std::cin.good()) return CommandResult::Continue;
            }

            if (validateLocalMailSettings(s, reason))
            {
                clearLocalProfileCache();
                bool ok1 = saveLocalMailSettings(s);
                bool ok2 = saveLocalContacts(cm.contacts(), cm.mailingLists());
                LocalProfileMeta meta;
                meta.source = "thunderbird";
                bool ok3 = saveLocalProfileMeta(meta);

                // Для текущего запуска переключаем runtime на локальные настройки сразу.
                tb.loadFromLocalCache(profile_, s);

                std::cout << "[CACHE] settings=" << (ok1 ? "ok" : "fail")
                          << " contacts=" << (ok2 ? "ok" : "fail")
                          << " meta=" << (ok3 ? "ok" : "fail") << "\n";

                std::cout << "[IMPORT] Ящик: " << (s.fullName.empty() ? "" : (s.fullName + " "))
                          << "<" << s.email << ">\n"
                          << "[IMPORT] IMAP: " << s.imapHost << ":" << s.imapPort
                          << " SSL=" << (s.imapSsl ? "yes" : "no")
                          << " USER=" << s.userName << "\n"
                          << "[IMPORT] SMTP: " << s.smtpHost << ":" << s.smtpPort
                          << " SSL=" << (s.smtpSsl ? "yes" : "no")
                          << " USER=" << s.userName << "\n"
                          << "[IMPORT] Контакты: " << cm.contacts().size()
                          << "  Списки: " << cm.mailingLists().size() << "\n";
            }
            else
            {
                std::cout << "[CACHE][WARN] Настройки невалидны и не сохранены: " << reason << "\n";
                my::pause();
                return CommandResult::Continue;
            }
        }

        if (!saveActiveProfile(profile_))
        {
            std::cout << "ОШИБКА: не удалось сохранить активный профиль\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "Аккаунт применён и профиль сохранён\n";
        pinMenu();
        return CommandResult::ExitMenu;

    }

} // namespace astra_mail
