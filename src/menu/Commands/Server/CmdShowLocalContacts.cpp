#include "CmdShowLocalContacts.h"

#include "core/profile/LocalProfileCache.h"
#include "Utils/Utils.h"

#include <iostream>
#include <algorithm>

namespace astra_mail
{
    std::string CmdShowLocalContacts::title() const
    {
        return "Показать абонентов (локальные)";
    }

    CommandResult CmdShowLocalContacts::execute()
    {
        std::unordered_map<std::string, Contact> contacts;
        std::unordered_map<std::string, std::vector<std::string>> lists;

        if (!loadLocalContacts(contacts, lists))
        {
            std::cout << "Локальные контакты не найдены.\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "=== Локальные контакты ===\n";
        std::cout << "Контактов: " << contacts.size() << "\n";
        std::cout << "Списков  : " << lists.size() << "\n\n";

        // Печатаем первые N, чтобы меню не превращалось в бесконечный вывод.
        const size_t limit = 80;
        size_t i = 0;
        for (const auto& kv : contacts)
        {
            if (i++ >= limit)
            {
                std::cout << "... (" << (contacts.size() - limit) << " ещё)\n";
                break;
            }
            const auto& c = kv.second;
            std::cout << "- " << c.email;
            if (!c.displayName.empty())
                std::cout << " | " << c.displayName;
            if (!c.nickName.empty())
                std::cout << " | nick=" << c.nickName;
            std::cout << "\n";
        }

        if (!lists.empty())
        {
            std::cout << "\n=== Списки рассылки ===\n";
            const size_t membersLimit = 50;
            for (const auto& kv : lists)
            {
                std::cout << "- " << kv.first << " (" << kv.second.size() << ")\n";
                size_t j = 0;
                for (const auto& email : kv.second)
                {
                    if (j++ >= membersLimit)
                    {
                        std::cout << "    ... (" << (kv.second.size() - membersLimit)
                                  << " ещё)\n";
                        break;
                    }
                    std::cout << "    * " << email << "\n";
                }
            }
        }

        my::pause();
        return CommandResult::Continue;
    }
}

