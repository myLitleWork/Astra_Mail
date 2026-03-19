#include "CmdEditLocalProfileSettings.h"

#include "Configs/Editor/EditorLauncher.h"
#include "core/profile/LocalProfileCache.h"
#include "core/idle/IdleBackground.h"

#include <iostream>
#include <string>

namespace astra_mail
{
    std::string CmdEditLocalProfileSettings::title() const
    {
        return "Редактировать настройки профиля (локальные)";
    }

    CommandResult CmdEditLocalProfileSettings::execute()
    {
        if (!astra_mail::ensureLocalMailSettingsFile())
        {
            std::cout << "[EDIT][ERROR] Не удалось подготовить файл настроек локального профиля\n";
            return CommandResult::Continue;
        }

        std::cout << "=== Подсказка по файлу mail_settings.conf ===\n"
                  << "Откроется файл с заголовком-комментариями.\n"
                  << "ВАЖНО: замените CHANGE_ME, imap.example.com и smtp.example.com на реальные значения.\n"
                  << "После сохранения лучше перезапустить программу (или выбрать профиль заново),\n"
                  << "чтобы кэш перечитал изменения.\n\n";

        // Внешний редактор блокирует текущий поток, поэтому пока он запущен
        // запрещаем IdleBackground выполнять фоновые действия (экспорт/отправку),
        // иначе можно получить конфликты при записи файлов.
        IdleBackground::BusyGuard busy;

        std::string err;
        bool ok = astra_mail::system::openInEditor(getLocalMailSettingsPath(), err);
        if (!ok)
        {
            std::cout << "[EDIT][ERROR] " << err << "\n";
        }

        // Сразу после выхода из редактора считаем действие пользователя активностью,
        // чтобы idle-действия не стартовали "сразу".
        IdleBackground::instance().resetTimer();

        return CommandResult::Continue;
    }
}

