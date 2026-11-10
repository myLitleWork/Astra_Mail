#include "ContactsRecipientResolver.h"
#include <cctype>
#include <sstream>
#include <iostream>


namespace astra_mail
{
    ContactsRecipientResolver::ContactsRecipientResolver(const ContactsManager& cm)
            : contacts_(cm.snapshot())
    {}

    bool ContactsRecipientResolver::looksLikeEmail(const std::string& s)
    {
        auto at = s.find('@');
        if (at == std::string::npos) return false;
        auto dot = s.find('.', at + 1);
        return dot != std::string::npos && dot + 1 < s.size();
    }

    std::string ContactsRecipientResolver::normalize(const std::string& s)
    {
        std::string r;
        r.reserve(s.size());

        bool prevUnderscore = false;

        for (unsigned char c : s)
        {
            if (std::isspace(c))
            {
                if (!r.empty() && !prevUnderscore)
                {
                    r.push_back('_');
                    prevUnderscore = true;
                }
            }
            else
            {
                r.push_back(std::tolower(c));
                prevUnderscore = false;
            }
        }

        // правый trim '_'
        if (!r.empty() && r.back() == '_')
            r.pop_back();

        return r;
    }

    optional<std::string>
    ContactsRecipientResolver::resolve(const std::string& input) const
    {
        // Allow explicit multi-recipient syntax in folder name.
        if (input.find(',') != std::string::npos || input.find(';') != std::string::npos)
            return input;

        if (looksLikeEmail(input))
            return input;

        const std::string key = normalize(input);
        optional<std::string> found;

        // mailing list takes precedence (циркулярная запись)
        {
            const auto& lists = contacts_.mailingLists;
            auto it = lists.find(key);
            if (it != lists.end() && !it->second.empty())
            {
                std::ostringstream oss;
                for (size_t i = 0; i < it->second.size(); ++i)
                {
                    if (i) oss << ",";
                    oss << it->second[i];
                }
                return oss.str();
            }
        }

        for (const auto& it : contacts_.contacts)
        {
            const auto& c = it.second;

            // nickName
            if (!c.nickName.empty() &&
                normalize(c.nickName) == key)
            {
                if (found) return optional<std::string>{};
                found = c.email;
                continue;
            }

            // displayName
            if (!c.displayName.empty() &&
                normalize(c.displayName) == key)
            {
                if (found) return optional<std::string>{};
                found = c.email;
                continue;
            }

            // email local-part
            auto at = c.email.find('@');
            if (at != std::string::npos &&
                normalize(c.email.substr(0, at)) == key)
            {
                if (found) return optional<std::string>{};
                found = c.email;
            }
        }


        if (!found)
        {
            // Диагностика: часто ошибка в несовпадении ключей (например, псевдоним списка vs имя).
            // Это не "комментарий ради комментария" — помогает быстро понять, почему письмо не попало в outbox.
            // (Выводится только если контакт/список не найден.)
            // NB: stdout тут допустим — резолвер используется в CLI.
            std::cout << "[RESOLVE][WARN] Не найден получатель для папки: \"" << input
                      << "\" (key=\"" << key << "\")\n";
        }

        return found; // empty optional если 0 или конфликт
    }
}
