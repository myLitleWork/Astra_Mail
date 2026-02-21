#pragma once
#include <string>

namespace astra_mail
{
    enum class CommandResult
    {
        Continue,
        ExitMenu
    };

    struct ICommand
    {
        virtual ~ICommand() = default;
        virtual std::string title() const = 0;
        virtual CommandResult execute() = 0;
    };
}
