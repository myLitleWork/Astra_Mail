#include "ThunderbirdSettings.h"
#include "core/mail/AccountAccess.h"
#include "Utils/SensitiveString.h"
#include <algorithm>


#include <fstream>
#include <regex>
#include <sstream>

static bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.size() >= prefix.size()
           && std::equal(prefix.begin(), prefix.end(), s.begin());
}

static bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size()
           && std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}


static std::string trim(std::string s)
{
    auto is_ws = [](char c){ return std::isspace(static_cast<unsigned char>(c)); };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_ws));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), is_ws).base(), s.end());
    return s;
}

static std::string unquote(const std::string& s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static bool parsePrefsFile(const std::string& path, PrefMap& out)
{
    std::ifstream in(path);
    if (!in.is_open())
        return false;

    std::regex re(R"(user_pref\("([^"]+)\",\s*(.+)\);)");

    std::string line;

    while (std::getline(in, line))
    {
        std::smatch m;
        if (!std::regex_search(line, m, re))
            continue;

        std::string key = m[1];
        std::string val = trim(m[2]);
        out[key] = unquote(val);
    }
    return true;
}


ThunderbirdSettings& ThunderbirdSettings::instance()
{
    static ThunderbirdSettings inst;
    return inst;
}

const std::unordered_map<std::string, AccountConfig>&

ThunderbirdSettings::accounts() const
{
    return accounts_;
}

const std::unordered_map<std::string, ImapConfig>&
ThunderbirdSettings::imapServers() const
{
    return imap_;
}

const std::unordered_map<std::string, SmtpConfig>&
ThunderbirdSettings::smtpServers() const
{
    return smtp_;
}

const std::unordered_map<std::string, IdentityConfig>&
ThunderbirdSettings::identities() const
{
    return identities_;
}

std::string ThunderbirdSettings::defaultAccount() const
{
    return defaultAccount_;
}

bool ThunderbirdSettings::load(const std::string& prefsPath)
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    clearMailPassword();
    accounts_.clear();
    imap_.clear();
    smtp_.clear();
    identities_.clear();
    active_ = {};
    defaultAccount_.clear();

    PrefMap p;
    if (!parsePrefsFile(prefsPath, p))
        return false;

    // default account
    auto itDef = p.find("mail.accountmanager.defaultaccount");
    if (itDef != p.end())
        defaultAccount_ = itDef->second;

    // accounts
    auto itAccs = p.find("mail.accountmanager.accounts");
    if (itAccs != p.end())
    {
        std::stringstream ss(itAccs->second);
        std::string acc;
        while (std::getline(ss, acc, ','))
        {
            AccountConfig a;
            a.accountId = acc;

            auto sit = p.find("mail.account." + acc + ".server");
            if (sit != p.end())
                a.serverId = sit->second;

            auto iit = p.find("mail.account." + acc + ".identities");
            if (iit != p.end())
            {
                std::stringstream is(iit->second);
                std::string id;
                while (std::getline(is, id, ','))
                    a.identities.push_back(id);
            }

            accounts_[acc] = a;
        }
    }

    // IMAP servers
    for (const auto& it : p)
    {

        const std::string& k = it.first;
        const std::string& v = it.second;

        if (!startsWith(k, "mail.server.") || !endsWith(k, ".type"))
            continue;

        if (v != "imap")
            continue;

        std::string id = k.substr(12, k.size() - 12 - 5);
        ImapConfig im;
        im.serverId = id;
        im.host = p["mail.server." + id + ".hostname"];
        im.user = p["mail.server." + id + ".userName"];
        im.ssl  = (p["mail.server." + id + ".socketType"] == "2");
        im.port = im.ssl ? 993 : 143;

        imap_[id] = im;
    }

    // SMTP servers
    for (const auto& it : p)
    {

        const std::string& k = it.first;
        const std::string& v = it.second;


        if (!startsWith(k, "mail.smtpserver.") || !endsWith(k, ".hostname"))
            continue;

        std::string id = k.substr(16, k.size() - 16 - 9);
        SmtpConfig sm;
        sm.serverId = id;
        sm.host = v;
        sm.user = p["mail.smtpserver." + id + ".username"];
        sm.ssl  = (p["mail.smtpserver." + id + ".try_ssl"] == "2");

        auto pit = p.find("mail.smtpserver." + id + ".port");
        sm.port = (pit != p.end()) ? std::stoi(pit->second) : (sm.ssl ? 465 : 25);

        smtp_[id] = sm;
    }

    // identities
    for (const auto& it : p)
    {

        const std::string& k = it.first;
        const std::string& v = it.second;

        if (!startsWith(k, "mail.identity.") || !endsWith(k, ".useremail"))
            continue;

        std::string id = k.substr(14, k.size() - 14 - 10);
        IdentityConfig ic;
        ic.identityId = id;
        ic.email = v;
        ic.fullName = p["mail.identity." + id + ".fullName"];
        ic.smtpServerId = p["mail.identity." + id + ".smtpServer"];
        ic.sentFolder = p["mail.identity." + id + ".fcc_folder"];

        identities_[id] = ic;
    }

    return true;
}

bool ThunderbirdSettings::applyDefaultAccount()
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    clearMailPassword();
    active_ = {}; // сброс

    auto accIt = accounts_.find(defaultAccount_);
    if (accIt == accounts_.end())
        return false;

    const auto& acc = accIt->second;
    if (acc.identities.empty())
        return false;

    const std::string& identityId = acc.identities.front();

    auto idIt = identities_.find(identityId);
    if (idIt == identities_.end())
        return false;

    const auto& id = idIt->second;

    active_.accountId     = acc.accountId;
    active_.identityId    = identityId;
    active_.imapServerId  = acc.serverId;
    active_.smtpServerId  = id.smtpServerId;

    return active_.valid();
}

const ActiveMailConfig& ThunderbirdSettings::active() const
{
    return active_;
}

std::string ThunderbirdSettings::secretContext() const
{
    if (!active_.valid()) return {};
    const auto im = imap_.find(active_.imapServerId);
    const auto sm = smtp_.find(active_.smtpServerId);
    const auto identity = identities_.find(active_.identityId);
    if (im == imap_.end() || sm == smtp_.end() || identity == identities_.end() ||
        im->second.host.empty() || sm->second.host.empty()) return {};
    auto host = [](std::string value) {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        while (!value.empty() && value.back() == '.') value.pop_back();
        return value;
    };
    std::string context = "AstraMail-account-1";
    auto add = [&](const std::string& value) { context += ":" + std::to_string(value.size()) + ":" + value; };
    add(currentProfile_.id); add(currentProfile_.path); add(active_.accountId);
    add(host(im->second.host)); add(std::to_string(im->second.port)); add(im->second.user); add(im->second.ssl ? "1" : "0");
    add(host(sm->second.host)); add(std::to_string(sm->second.port)); add(sm->second.user); add(sm->second.ssl ? "1" : "0");
    add(identity->second.email);
    return context;
}

bool ThunderbirdSettings::hasMailPassword() const
{
    return !mailPassword_.empty() && !passwordContext_.empty() && passwordContext_ == secretContext();
}

const std::string& ThunderbirdSettings::mailPassword() const
{
    static const std::string empty;
    return hasMailPassword() ? mailPassword_ : empty;
}

void ThunderbirdSettings::clearMailPassword()
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return;
    astra_mail::clearSensitiveString(mailPassword_); passwordContext_.clear();
}

bool ThunderbirdSettings::setMailPassword(const std::string& pass)
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    const auto context = secretContext();
    clearMailPassword();
    if (context.empty() || pass.empty()) return false;
    mailPassword_ = pass; passwordContext_ = context;
    return true;
}

bool ThunderbirdSettings::setCurrentProfile(const astra_mail::TbProfile& profile)
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    if (profile.id != currentProfile_.id || profile.path != currentProfile_.path) clearMailPassword();
    currentProfile_ = profile;
    return true;
}

bool ThunderbirdSettings::loadFromLocalCache(const astra_mail::TbProfile& profile, const astra_mail::LocalMailSettings& s)
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    clearMailPassword();
    accounts_.clear();
    imap_.clear();
    smtp_.clear();
    identities_.clear();
    active_ = {};

    currentProfile_ = profile;

    AccountConfig acc;
    acc.accountId = s.accountId.empty() ? "local_account_1" : s.accountId;
    acc.serverId = "local_imap_1";
    acc.identities.push_back(s.identityId.empty() ? "local_identity_1" : s.identityId);

    ImapConfig im;
    im.serverId = acc.serverId;
    im.host = s.imapHost;
    im.port = s.imapPort;
    im.ssl = s.imapSsl;
    im.user = s.userName;

    SmtpConfig sm;
    sm.serverId = "local_smtp_1";
    sm.host = s.smtpHost;
    sm.port = s.smtpPort;
    sm.ssl = s.smtpSsl;
    sm.user = s.userName;

    IdentityConfig id;
    id.identityId = acc.identities.front();
    id.email = s.email;
    id.fullName = s.fullName;
    id.smtpServerId = sm.serverId;

    accounts_[acc.accountId] = acc;
    imap_[im.serverId] = im;
    smtp_[sm.serverId] = sm;
    identities_[id.identityId] = id;
    defaultAccount_ = acc.accountId;

    active_.accountId = acc.accountId;
    active_.identityId = id.identityId;
    active_.imapServerId = im.serverId;
    active_.smtpServerId = sm.serverId;
    return true;
}


bool ThunderbirdSettings::applyAccount(const std::string& accountId)
{
    astra_mail::AccountChangeScope change;
    if (!change.allowed()) return false;
    clearMailPassword();
    active_ = {}; // сброс

    // 1. аккаунт
    auto accIt = accounts_.find(accountId);
    if (accIt == accounts_.end())
        return false;

    const auto& acc = accIt->second;

    // 2. identity (пока берём первую — ОСОЗНАННО)
    if (acc.identities.empty())
        return false;

    const std::string& identityId = acc.identities.front();

    auto idIt = identities_.find(identityId);
    if (idIt == identities_.end())
        return false;

    const auto& id = idIt->second;

    // 3. серверы
    if (imap_.find(acc.serverId) == imap_.end())
        return false;

    if (smtp_.find(id.smtpServerId) == smtp_.end())
        return false;

    // 4. фиксируем активную конфигурацию
    active_.accountId    = acc.accountId;
    active_.identityId   = identityId;
    active_.imapServerId = acc.serverId;
    active_.smtpServerId = id.smtpServerId;

    return active_.valid();
}


