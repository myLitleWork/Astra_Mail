#pragma once
#include <string>
#if __has_include(<optional>)
#include <optional>
  using std::optional;
#else
#include <experimental/optional>
using std::experimental::optional;
#endif


namespace astra_mail
{
    class IRecipientResolver
    {
    public:
        virtual ~IRecipientResolver() = default;

        // вернуть email по имени папки
        // std::nullopt → невалидный получатель
        [[nodiscard]] virtual optional<std::string>
        resolve(const std::string& folderName) const = 0;
    };
}
