#include "menu/MailCommandAccess.h"
#include "CmdViewOutboxMails.h"
#include "core/mail/MailFolderScanner.h"
#include "core/mail/MailFolderReport.h"
#include "Utils/Utils.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "core/contacts/ContactsManager.h"
#include "UtilsFS/FsCompat.h"
#include "core/paths/AstraMailPaths.h"
#include <stdexcept>
#include <iostream>

namespace astra_mail
{
    CommandResult CmdViewOutboxMails::execute()
    {
        MailOperationScope operation(std::try_to_lock);
        if (!mailCommandAllowed(operation.allowed())) return CommandResult::Continue;
        auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());

        MailFolderScanner scan(
                MailFolderScanOptions{
                        .ignoreHidden = true,
                        .ignoreEmptyMailFolders = true,
                        .allowSubfolders = false
                },
                resolver
        );

        auto outboxPath = getOutboxRoot();
        auto result = scan.scanMount(outboxPath);

        MailFolderReport::print(std::cout, result);
        my::pause();
        return CommandResult::Continue;
    }

    CmdViewOutboxMails::CmdViewOutboxMails() : ICommand()
    {
    }

    std::string CmdViewOutboxMails::title() const {
        return "Просмотр писем в исходящих";
    }
}
