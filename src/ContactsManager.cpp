#include "ContactsManager.h"
#include "ThunderbirdContactsImporter.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"

namespace astra_mail {


    ContactsManager &ContactsManager::instance() {
        static ContactsManager inst;
        return inst;
    }

    bool ContactsManager::loadForProfile(const astra_mail::TbProfile &profile, bool& _hasDub) {
        clear();

        ThunderbirdContactsImporter importer;

        if (!importer.loadOneBook(profile.path + "/abook.sqlite"))
            return false;

        _hasDub = importer.hasDuplicateKeys();

        contacts_ = importer.contacts();
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

    void ContactsManager::clear() {
        contacts_.clear();
        activeProfileId_.clear();
    }

    const std::string &ContactsManager::activeProfileId() const {
        return activeProfileId_;
    }

}
