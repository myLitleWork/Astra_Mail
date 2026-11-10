#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include "core/thunderbird/ThunderbirdSettings.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "core/contacts/ThunderbirdContactsImporter.h"

namespace astra_mail {

    struct ContactsSnapshot {
        std::string profileId;
        std::unordered_map<std::string, Contact> contacts;
        std::unordered_map<std::string, std::vector<std::string>> mailingLists;
    };

    class ContactsManager {
    public:
        static ContactsManager &instance();

        // загрузить контакты для выбранного профиля
        bool loadForProfile(const TbProfile &profile, bool& hasDub);

        // есть ли загруженные контакты
        bool hasContacts() const;

        // доступ к контактам активного профиля
        std::unordered_map<std::string, Contact> contacts() const;

        // списки рассылки (normalized name -> emails)
        std::unordered_map<std::string, std::vector<std::string>> mailingLists() const;
        ContactsSnapshot snapshot() const;

        // очистка (при смене профиля / выходе)
        bool clear();

        // мета
        std::string activeProfileId() const;
        bool loadFromCache(const std::string& profileId,
                           const std::unordered_map<std::string, Contact>& contacts,
                           const std::unordered_map<std::string, std::vector<std::string>>& mailingLists);

    private:
        ContactsManager() = default;
        mutable std::mutex mutex_;

        std::string activeProfileId_;
        std::unordered_map<std::string, Contact> contacts_;
        std::unordered_map<std::string, std::vector<std::string>> mailingLists_;

    };
}
