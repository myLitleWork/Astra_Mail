#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include "core/thunderbird/ThunderbirdSettings.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "core/contacts/ThunderbirdContactsImporter.h"

namespace astra_mail {

    class ContactsManager {
    public:
        static ContactsManager &instance();

        // загрузить контакты для выбранного профиля
        bool loadForProfile(const TbProfile &profile, bool& hasDub);

        // есть ли загруженные контакты
        bool hasContacts() const;

        // доступ к контактам активного профиля
        const std::unordered_map<std::string, Contact> &contacts() const;

        // списки рассылки (normalized name -> emails)
        const std::unordered_map<std::string, std::vector<std::string>>& mailingLists() const;

        // очистка (при смене профиля / выходе)
        void clear();

        // мета
        const std::string &activeProfileId() const;

    private:
        ContactsManager() = default;

        std::string activeProfileId_;
        std::unordered_map<std::string, Contact> contacts_;
        std::unordered_map<std::string, std::vector<std::string>> mailingLists_;

    };
}
