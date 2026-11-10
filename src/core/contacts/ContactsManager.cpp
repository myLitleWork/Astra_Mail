#include "ContactsManager.h"
#include "core/mail/AccountAccess.h"
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
        MailDataChangeScope change;
        if (!change.allowed()) return false;
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

        return loadFromCache(profile.id, importer.contacts(), importer.mailingLists());
    }

    bool ContactsManager::hasContacts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !contacts_.empty();
    }

    std::unordered_map<std::string, Contact> ContactsManager::contacts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return contacts_;
    }

    std::unordered_map<std::string, std::vector<std::string>> ContactsManager::mailingLists() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return mailingLists_;
    }

    ContactsSnapshot ContactsManager::snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ContactsSnapshot{activeProfileId_, contacts_, mailingLists_};
    }

    bool ContactsManager::clear() {
        MailDataChangeScope change;
        if (!change.allowed()) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        contacts_.clear(); mailingLists_.clear(); activeProfileId_.clear();
        return true;
    }

    std::string ContactsManager::activeProfileId() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeProfileId_;
    }

    bool ContactsManager::loadFromCache(const std::string& profileId,
            const std::unordered_map<std::string, Contact>& contacts,
            const std::unordered_map<std::string, std::vector<std::string>>& mailingLists) {
        MailDataChangeScope change;
        if (!change.allowed()) return false;
        // Build copies before publishing, so failed allocations cannot expose a
        // mixture of the old and new contact/list/profile tables.
        auto newContacts = contacts;
        auto newLists = mailingLists;
        auto newProfile = profileId;
        std::lock_guard<std::mutex> lock(mutex_);
        contacts_.swap(newContacts); mailingLists_.swap(newLists); activeProfileId_.swap(newProfile);
        return true;
    }
}