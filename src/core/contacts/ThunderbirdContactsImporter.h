#pragma once

#include "UtilsFS/FsCompat.h"

#include <unordered_map>
#include <vector>
#include <string>

namespace astra_mail
{
    struct Contact
    {
        std::string key;        // NickName | DisplayName | email
        std::string email;
        std::string displayName;
        std::string nickName;
    };


    class ThunderbirdContactsImporter
    {
    public:

        bool loadOneBook(const fs::path& dbPath);

        const std::unordered_map<std::string, Contact>& contacts() const;
        const std::unordered_map<std::string, std::vector<std::string>>& mailingLists() const;

        bool hasDuplicateKeys() const;

    private:


        std::unordered_map<std::string, Contact> contacts_;
        std::unordered_map<std::string, std::vector<std::string>> mailingLists_; // normalized list name -> emails
        bool hasDuplicates_ = false;
    };
}


