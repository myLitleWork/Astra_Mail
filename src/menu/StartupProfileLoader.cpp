#include "StartupProfileLoader.h"

#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/ActiveProfileStorage.h"
#include "core/profile/LocalProfileCache.h"
#include "menu/MenuCreator.h"
#include "core/contacts/ContactsManager.h"
#include "Utils/Utils.h"
#include "UtilsFS/FsCompat.h"
#include "Utils/ImapClient.h"
#include "Utils/SmtpClient.h"

#include <iostream>

namespace astra_mail
{
    bool pinMenu()
    {
        auto& sm = SecretManager::instance();
        auto& tb = ThunderbirdSettings::instance();

        Menu menu("Безопасность");

        if (sm.isInitialized())
        {
            menu.add(std::make_unique<CmdEnterPin>());
            menu.add(std::make_unique<CmdResetPin>());
        }
        else
        {
        // первый запуск
            menu.add(std::make_unique<CmdResetPin>());
        }

        menu.run();

        // ЕДИНСТВЕННЫЙ критерий успеха
        return tb.hasMailPassword();
    }

    bool tryLoadSavedProfile(TbProfile& outProfile)
    {
        // 0. Приоритет: пробуем загрузить локальный кэш профиля.
        {
            LocalMailSettings local{};
            if (loadLocalMailSettings(local))
            {
                std::string reason;
                if (validateLocalMailSettings(local, reason))
                {
                    outProfile = TbProfile{};
                    outProfile.id = local.profileId.empty() ? "local_profile" : local.profileId;
                    outProfile.path = local.profilePath;
                    outProfile.active_account_id = local.accountId;

                    auto& tb = ThunderbirdSettings::instance();
                    tb.loadFromLocalCache(outProfile, local);

                    std::unordered_map<std::string, Contact> contacts;
                    std::unordered_map<std::string, std::vector<std::string>> mailingLists;
                    loadLocalContacts(contacts, mailingLists);
                    ContactsManager::instance().loadFromCache(outProfile.id, contacts, mailingLists);

                    std::cout << "[STARTUP] Загружены локальные настройки профиля из cache\n";
                    return true;
                }

                std::cout << "[STARTUP][WARN] Локальные настройки невалидны: " << reason << "\n";
            }
        }

        // 1. пробуем прочитать сохранённый профиль
        if (!loadActiveProfile(outProfile))
            return false;

        if (outProfile.path.empty())
            return false;

        // 2. проверяем prefs.js
        fs::path prefsPath = fs::path(outProfile.path) / "prefs.js";
        if (!fs::exists(prefsPath))
            return false;

        // 3. инициализируем ThunderbirdSettings
        auto& tb = ThunderbirdSettings::instance();

        tb.setCurrentProfile(outProfile);

        if (!tb.load(prefsPath.string()))
            return false;

        if (!tb.applyAccount(outProfile.active_account_id))
        {
            std::cout << "Аккаунт << "  + outProfile.active_account_id +  " << не применён\n";
            my::pause();
        }

        bool hasDub = false;
        auto& cm = ContactsManager::instance();

        if (!cm.loadForProfile(outProfile, hasDub))
        {
            std::cout << "Контакты не загружены\n";
            my::pause();
        }
        if (hasDub)
        {
            std::cout << "ВНИМАНИЕ: найдены одинаковые никнеймы контактов\n";
            my::pause();
        }

        return true;
    }

    bool profileSetupMenu()
    {

        auto menu = menu::createThunderbirdProfilesMenu();
        menu->run();
        std::cout << "[STARTUP] Вышли из меню выбора профилей...\n";
        auto& tb = ThunderbirdSettings::instance();
        const TbProfile& profile = tb.currentProfile();

        // КРИТИЧНО: проверяем, что выбор реально был
        if (profile.id.empty() || profile.path.empty())
        {
            std::cout << "[STARTUP][WARN] Профиль не выбран\n";
            return false;
        }

        if (!saveActiveProfile(profile))
        {
            std::cout << "[STARTUP][ERROR] Не удалось сохранить профиль\n";
            return false;
        }

        std::cout << "[STARTUP] Активный профиль сохранён: "
                  << profile.id << "\n"
                  << "[STARTUP] Путь: " << profile.path << "\n";

        return true;
    }

    bool checkServers()
    {
        auto& tb = ThunderbirdSettings::instance();

        if (!tb.active().valid())
        {
            std::cout << "[Проверка серверов] Аккаунт не выбран.\n";
            my::pause();
            return false;
        }

        if (!tb.hasMailPassword())
        {
            std::cout << "[Проверка серверов] Пароль почты не задан.\n";
            my::pause();
            return false;
        }

        const auto& active = tb.active();
        const auto& imap = tb.imapServers().at(active.imapServerId);
        const auto& smtpConfig = tb.smtpServers().at(active.smtpServerId);
        const std::string& password = tb.mailPassword();

        std::string err;

        // Проверка IMAP
        std::vector<std::string> folders;
        if (!ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, folders, err))
        {
            std::cout << "[Проверка серверов] IMAP недоступен: " << imap.host << ":" << imap.port
                      << " — " << err << "\n";
            my::pause();
            return false;
        }

        // Проверка SMTP
        SmtpClient smtp(smtpConfig.host, smtpConfig.port);
        smtp.setCredentials(smtpConfig.user, password);
        SmtpClient::Session ses;
        if (!smtp.openSession(ses, err))
        {
            std::cout << "[Проверка серверов] SMTP недоступен: " << smtpConfig.host << ":" << smtpConfig.port
                      << " — " << err << "\n";
            my::pause();
            return false;
        }
        SmtpClient::closeSession(ses);

        std::cout << "[Проверка серверов] IMAP и SMTP доступны.\n";
        return true;
    }

    bool checkServersSilent()
    {
        auto& tb = ThunderbirdSettings::instance();
        if (!tb.active().valid() || !tb.hasMailPassword())
            return false;

        const auto& active = tb.active();
        const auto& imap = tb.imapServers().at(active.imapServerId);
        const auto& smtpConfig = tb.smtpServers().at(active.smtpServerId);
        const std::string& password = tb.mailPassword();
        std::string err;

        std::vector<std::string> folders;
        if (!ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, folders, err))
            return false;

        SmtpClient smtp(smtpConfig.host, smtpConfig.port);
        smtp.setCredentials(smtpConfig.user, password);
        SmtpClient::Session ses;
        if (!smtp.openSession(ses, err))
            return false;
        SmtpClient::closeSession(ses);
        return true;
    }

} // namespace astra_mail
