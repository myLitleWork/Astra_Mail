#include "CmdViewOutboxMails.h"
#include "MailFolderScanner.h"
#include "MailFolderReport.h"
#include "src/Utils/Utils.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "src/ContactsManager.h"
#include "UtilsFS/FsCompat.h"
#include <stdexcept>
#include <iostream>

namespace astra_mail
{
    static fs::path getOutboxRoot()
    {
        const char* home = getenv("HOME");
        if (!home)
            throw std::runtime_error("HOME not set");

        fs::path root = fs::path(home) / ".local/share/AstraMail/outbox";
        return root;
    }

    CommandResult CmdViewOutboxMails::execute()
    {
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
        return "Просмотр писем в outbox";
    }
}
