#pragma once
#include "menu/Command.h"
#include <ostream>

namespace astra_mail
{
    struct SendCycleReport {
        size_t completedMessages = 0, confirmedRecipients = 0;
        size_t queuedMessages = 0, awaitingRecipients = 0, unconfirmedRecipients = 0;
        bool cancelled = false;
        std::string lastError;
    };
    class CmdSendMailsFromOutbox : public ICommand
    {
    public:
        CmdSendMailsFromOutbox();
        std::string title() const override;
        CommandResult execute() override;

        /** Отправка писем из outbox. log == nullptr — тихий режим (для фона). Возвращает true при успехе или если нечего отправлять. */
        static bool runSendFromOutbox(std::ostream* log, SendCycleReport* report = nullptr);
    };
}
