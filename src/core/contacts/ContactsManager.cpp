#include "ContactsManager.h"
#include "ThunderbirdContactsImporter.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "UtilsFS/FsCompat.h"

#include <vector>

namespace astra_mail {


    ContactsManager &ContactsManager::instance() {
        static ContactsManager inst;
        return inst;
    }

    bool ContactsManager::loadForProfile(const astra_mail::TbProfile &profile, bool& _hasDub) {
        clear();

        ThunderbirdContactsImporter importer;

        const fs::path profileDir = fs::path(profile.path);
        std::vector<fs::path> books;

        // Thunderbird address books are usually here
        books.push_back(profileDir / "abook.sqlite");
        books.push_back(profileDir / "history.sqlite");

        // Additional address books: abook-*.sqlite (and similar)
        {
            std::error_code ec;
            if (fs::is_directory(profileDir, ec))
            {
                for (const auto& entry : fs::directory_iterator(profileDir, ec))
                {
                    if (ec) break;
                    const fs::path p = entry.path();
                    if (p.extension() != ".sqlite") continue;
                    const std::string name = p.filename().string();
                    if (name.rfind("abook-", 0) == 0)
                        books.push_back(p);
                }
            }
        }

        bool okAny = false;
        for (const auto& dbPath : books)
        {
            if (importer.loadOneBook(dbPath))
                okAny = true;
        }

        if (!okAny)
            return false;

        _hasDub = importer.hasDuplicateKeys();

        contacts_ = importer.contacts();
        mailingLists_ = importer.mailingLists();
        activeProfileId_ = profile.id;

        return true;
    }

    bool ContactsManager::hasContacts() const {
        return !contacts_.empty();
    }

    const std::unordered_map<std::string, astra_mail::Contact> &
    ContactsManager::contacts() const {
        return contacts_;
    }

    const std::unordered_map<std::string, std::vector<std::string>>&
    ContactsManager::mailingLists() const
    {
        return mailingLists_;
    }

    void ContactsManager::clear() {
        contacts_.clear();
        mailingLists_.clear();
        activeProfileId_.clear();
    }

    const std::string &ContactsManager::activeProfileId() const {
        return activeProfileId_;
    }

}
