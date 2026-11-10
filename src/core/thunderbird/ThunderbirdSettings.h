#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "core/profile/LocalProfileCache.h"

using PrefMap = std::unordered_map<std::string, std::string>;

struct ImapConfig {
    std::string serverId;
    std::string host;
    int port = 143;
    bool ssl = false;
    std::string user;
};

struct SmtpConfig {
    std::string serverId;
    std::string host;
    int port = 25;
    bool ssl = false;
    std::string user;
};

struct IdentityConfig {
    std::string identityId;
    std::string email;
    std::string fullName;
    std::string smtpServerId;
    std::string sentFolder;
};

struct AccountConfig {
    std::string accountId;
    std::string serverId;
    std::vector<std::string> identities;
};

struct ActiveMailConfig {

    std::string accountId;
    std::string identityId;
    std::string imapServerId;
    std::string smtpServerId;

    bool valid() const {
        return !accountId.empty()
               && !identityId.empty()
               && !imapServerId.empty()
               && !smtpServerId.empty();
    }
};

enum class ProfileState {
    NotLoaded,      // prefs.js ещё не читали
    DefaultLoaded,  // default применён автоматически
    UserSelected    // пользователь выбрал профиль вручную
};

struct SecretStorage {
    std::vector<uint8_t> encryptedPassword;
    std::vector<uint8_t> salt;
};




class ThunderbirdSettings {

public:

    static ThunderbirdSettings& instance();

public:
    bool setMailPassword(const std::string& pass);
    const std::string& mailPassword() const;
    bool hasMailPassword() const;
    void clearMailPassword();
    std::string secretContext() const;


    // сохранить мета-инфу выбранного профиля (для красивого вывода в меню)
    bool setCurrentProfile(const astra_mail::TbProfile& p);
    const astra_mail::TbProfile& currentProfile() const { return currentProfile_; }
    bool hasCurrentProfile() const { return !currentProfile_.path.empty(); }
    bool applyAccount(const std::string& accountId);


    bool load(const std::string& prefsPath);

    const std::unordered_map<std::string, AccountConfig>& accounts() const;
    const std::unordered_map<std::string, ImapConfig>& imapServers() const;
    const std::unordered_map<std::string, SmtpConfig>& smtpServers() const;
    const std::unordered_map<std::string, IdentityConfig>& identities() const;

    bool applyDefaultAccount();
    const ActiveMailConfig& active() const;
    bool loadFromLocalCache(const astra_mail::TbProfile& profile, const astra_mail::LocalMailSettings& s);

    std::string defaultAccount() const;

private:
    ThunderbirdSettings() = default;

    std::unordered_map<std::string, AccountConfig> accounts_;
    std::unordered_map<std::string, ImapConfig> imap_;
    std::unordered_map<std::string, SmtpConfig> smtp_;
    std::unordered_map<std::string, IdentityConfig> identities_;
    std::string defaultAccount_;
    ActiveMailConfig active_;
    astra_mail::TbProfile currentProfile_;
    std::string mailPassword_;
    std::string passwordContext_;

};
