#include "ContactsRecipientResolver.h"
#include <cctype>




namespace astra_mail
{
    ContactsRecipientResolver::ContactsRecipientResolver(const ContactsManager& cm)
            : contacts_(cm)
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
        if (looksLikeEmail(input))
            return input;

        const std::string key = normalize(input);
        optional<std::string> found;

        for (const auto& it : contacts_.contacts())
        {
            const auto& c = it.second;

            // nickName
            if (!c.nickName.empty() &&
                normalize(c.nickName) == key)
            {
                if (found) return std::experimental::nullopt;
                found = c.email;
                continue;
            }

            // displayName
            if (!c.displayName.empty() &&
                normalize(c.displayName) == key)
            {
                if (found) return std::experimental::nullopt;
                found = c.email;
                continue;
            }

            // email local-part
            auto at = c.email.find('@');
            if (at != std::string::npos &&
                normalize(c.email.substr(0, at)) == key)
            {
                if (found) return std::experimental::nullopt;
                found = c.email;
            }
        }


        return found; // nullopt если 0 или конфликт
    }
}
