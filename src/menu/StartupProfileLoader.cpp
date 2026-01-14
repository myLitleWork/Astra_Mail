#include "StartupProfileLoader.h"

#include "ThunderbirdSettings.h"
#include "ActiveProfileStorage.h"
#include "menu/MenuCreator.h"
#include "ContactsManager.h"
#include "Utils/Utils.h"
#include "UtilsFS/FsCompat.h"

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



} // namespace astra_mail
