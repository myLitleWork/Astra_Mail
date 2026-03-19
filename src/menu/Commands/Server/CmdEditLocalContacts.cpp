#include "CmdEditLocalContacts.h"

#include "Configs/Editor/EditorLauncher.h"
#include "core/profile/LocalProfileCache.h"
#include "core/idle/IdleBackground.h"

#include <iostream>

namespace astra_mail
{
    std::string CmdEditLocalContacts::title() const
    {
        return "Редактировать абонентов (локальные)";
    }

    CommandResult CmdEditLocalContacts::execute()
    {
        if (!astra_mail::ensureLocalContactsFile())
        {
            std::cout << "[EDIT][ERROR] Не удалось подготовить файл локальных контактов\n";
            return CommandResult::Continue;
        }

        std::cout << "=== Подсказка по файлу contacts.conf ===\n"
                  << "Формат строк:\n"
                  << "  C|key|email|displayName|nickName\n"
                  << "  L|list_name|email1,email2,email3\n\n"
                  << "Как работает key:\n"
                  << "  - если есть nickName: key = nickName\n"
                  << "  - иначе если есть displayName: key = displayName\n"
                  << "  - иначе: key = email\n\n"
                  << "Списки рассылки используйте так: list_name -> перечисление email через запятую.\n"
                  << "После сохранения лучше перезапустить программу (или выбрать профиль заново).\n\n";

        // Пока внешний редактор активен — запрещаем фоновые idle-действия.
        IdleBackground::BusyGuard busy;

        std::string err;
        bool ok = astra_mail::system::openInEditor(getLocalContactsPath(), err);
        if (!ok)
        {
            std::cout << "[EDIT][ERROR] " << err << "\n";
        }

        // Сбрасываем таймер, чтобы после закрытия редактора idle не стартовал сразу.
        IdleBackground::instance().resetTimer();

        return CommandResult::Continue;
    }
}

