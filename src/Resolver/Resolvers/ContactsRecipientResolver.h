#pragma once

#include "Resolver/RecipientResolver.h"
#include "core/contacts/ContactsManager.h"


#if __has_include(<optional>)
#include <optional>
  using std::optional;
#else
#include <experimental/optional>
using std::experimental::optional;
#endif

namespace astra_mail
{
    class ContactsRecipientResolver : public IRecipientResolver
    {
    public:
        explicit ContactsRecipientResolver(const ContactsManager& cm);

        optional<std::string>
        resolve(const std::string& input) const override;

    private:
        const ContactsManager& contacts_;

        static bool looksLikeEmail(const std::string& s);
        static std::string normalize(const std::string& s);
    };
}
