#include "CmdViewFlashMails.h"
#include "MailFolderScanner.h"
#include "MailFolderReport.h"
#include "src/Utils/Utils.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "src/ContactsManager.h"

#include <iostream>

namespace astra_mail
{
    CommandResult CmdViewFlashMails::execute()
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

        auto result = scan.scanMount(path_);

        MailFolderReport::print(std::cout, result);
        my::pause();   // ← ВОТ ЭТО КРИТИЧНО
        return CommandResult::Continue;
    }

    CmdViewFlashMails::CmdViewFlashMails(const std::string& path) : ICommand(), path_(path) {

    }

    std::string CmdViewFlashMails::title() const {
         return "Анализ писем в " + path_;
    }


}
