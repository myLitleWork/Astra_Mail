#pragma once
#include "Resolver/RecipientResolver.h"

namespace astra_mail
{
    class StubRecipientResolver : public IRecipientResolver
    {
    public:
        [[nodiscard]] optional<std::string>
        resolve(const std::string& folderName) const override;
    };
}
