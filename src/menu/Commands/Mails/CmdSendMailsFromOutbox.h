#pragma once
#include "menu/Command.h"
#include <ostream>

namespace astra_mail
{
    class CmdSendMailsFromOutbox : public ICommand
    {
    public:
        CmdSendMailsFromOutbox();
        std::string title() const override;
        CommandResult execute() override;

        /** Отправка писем из outbox. log == nullptr — тихий режим (для фона). Возвращает true при успехе или если нечего отправлять. */
        static bool runSendFromOutbox(std::ostream* log);
    };
}
