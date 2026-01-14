#pragma once

#include <unordered_map>
#include <string>
#include "ThunderbirdSettings.h"
#include "src/menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "ThunderbirdContactsImporter.h"

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

        // очистка (при смене профиля / выходе)
        void clear();

        // мета
        const std::string &activeProfileId() const;

    private:
        ContactsManager() = default;

        std::string activeProfileId_;
        std::unordered_map<std::string, Contact> contacts_;

    };
}
