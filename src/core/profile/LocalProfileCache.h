#pragma once

#include "UtilsFS/FsCompat.h"
#include "core/contacts/ThunderbirdContactsImporter.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace astra_mail
{
    struct LocalMailSettings
    {
        std::string profileId;
        std::string profilePath;
        std::string accountId;
        std::string identityId;

        std::string email;
        std::string fullName;
        std::string userName;

        std::string imapHost;
        int imapPort = 143;
        bool imapSsl = false;

        std::string smtpHost;
        int smtpPort = 25;
        bool smtpSsl = false;
    };

    struct LocalProfileMeta
    {
        int schemaVersion = 1;
        std::string source = "thunderbird";
        std::string importedAtUtc;
    };

    struct LocalProfileCacheData
    {
        LocalMailSettings settings;
        std::unordered_map<std::string, Contact> contacts;
        std::unordered_map<std::string, std::vector<std::string>> mailingLists;
        LocalProfileMeta meta;
    };

    fs::path getLocalProfileCacheDir();
    fs::path getLocalMailSettingsPath();
    fs::path getLocalContactsPath();
    fs::path getLocalMetaPath();

    bool saveLocalMailSettings(const LocalMailSettings& settings);
    bool loadLocalMailSettings(LocalMailSettings& out);

    bool saveLocalContacts(
            const std::unordered_map<std::string, Contact>& contacts,
            const std::unordered_map<std::string, std::vector<std::string>>& mailingLists);
    bool loadLocalContacts(
            std::unordered_map<std::string, Contact>& outContacts,
            std::unordered_map<std::string, std::vector<std::string>>& outMailingLists);

    bool saveLocalProfileMeta(const LocalProfileMeta& meta);
    bool loadLocalProfileMeta(LocalProfileMeta& out);

    bool validateLocalMailSettings(const LocalMailSettings& s, std::string& reason);
    void clearLocalProfileCache();

    // Создать файлы локального профиля с шаблонами, если их ещё нет.
    // Это нужно для “редактирования через внешний редактор”.
    bool ensureLocalMailSettingsFile();
    bool ensureLocalContactsFile();
}
