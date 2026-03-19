#include "LocalProfileCache.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

namespace astra_mail
{
    static std::string trim(std::string s)
    {
        auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isWs));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), isWs).base(), s.end());
        return s;
    }

    static std::vector<std::string> split(const std::string& s, char sep)
    {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string part;
        while (std::getline(ss, part, sep))
            out.push_back(part);
        return out;
    }

    static bool parseBool(const std::string& s)
    {
        return s == "1" || s == "true" || s == "yes" || s == "on";
    }

    static int parseIntOr(const std::string& s, int def)
    {
        try
        {
            return std::stoi(s);
        }
        catch (...)
        {
            return def;
        }
    }

    static std::string nowUtcString()
    {
        std::time_t t = std::time(nullptr);
        std::tm tmUtc{};
#if defined(_WIN32)
        gmtime_s(&tmUtc, &t);
#else
        gmtime_r(&t, &tmUtc);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
        return std::string(buf);
    }

    static fs::path getBaseDir()
    {
        const char* home = std::getenv("HOME");
        fs::path base = (home && *home) ? fs::path(home) : fs::current_path();
        return base / ".astramail" / "profile_cache";
    }

    fs::path getLocalProfileCacheDir()
    {
        return getBaseDir();
    }

    fs::path getLocalMailSettingsPath()
    {
        return getBaseDir() / "mail_settings.conf";
    }

    fs::path getLocalContactsPath()
    {
        return getBaseDir() / "contacts.conf";
    }

    fs::path getLocalMetaPath()
    {
        return getBaseDir() / "meta.conf";
    }

    static bool ensureCacheDir()
    {
        std::error_code ec;
        fs::create_directories(getBaseDir(), ec);
        return !ec;
    }

    bool saveLocalMailSettings(const LocalMailSettings& settings)
    {
        if (!ensureCacheDir())
            return false;

        std::ofstream out(getLocalMailSettingsPath().string(), std::ios::trunc);
        if (!out.is_open())
            return false;

        out << "profile_id=" << settings.profileId << "\n";
        out << "profile_path=" << settings.profilePath << "\n";
        out << "account_id=" << settings.accountId << "\n";
        out << "identity_id=" << settings.identityId << "\n";
        out << "email=" << settings.email << "\n";
        out << "full_name=" << settings.fullName << "\n";
        out << "user_name=" << settings.userName << "\n";
        out << "imap_host=" << settings.imapHost << "\n";
        out << "imap_port=" << settings.imapPort << "\n";
        out << "imap_ssl=" << (settings.imapSsl ? "1" : "0") << "\n";
        out << "smtp_host=" << settings.smtpHost << "\n";
        out << "smtp_port=" << settings.smtpPort << "\n";
        out << "smtp_ssl=" << (settings.smtpSsl ? "1" : "0") << "\n";

        return static_cast<bool>(out);
    }

    bool loadLocalMailSettings(LocalMailSettings& out)
    {
        out = LocalMailSettings{};

        std::ifstream in(getLocalMailSettingsPath().string());
        if (!in.is_open())
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            const auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            const std::string key = trim(line.substr(0, pos));
            const std::string val = trim(line.substr(pos + 1));

            if (key == "profile_id") out.profileId = val;
            else if (key == "profile_path") out.profilePath = val;
            else if (key == "account_id") out.accountId = val;
            else if (key == "identity_id") out.identityId = val;
            else if (key == "email") out.email = val;
            else if (key == "full_name") out.fullName = val;
            else if (key == "user_name") out.userName = val;
            else if (key == "imap_host") out.imapHost = val;
            else if (key == "imap_port") out.imapPort = parseIntOr(val, 143);
            else if (key == "imap_ssl") out.imapSsl = parseBool(val);
            else if (key == "smtp_host") out.smtpHost = val;
            else if (key == "smtp_port") out.smtpPort = parseIntOr(val, 25);
            else if (key == "smtp_ssl") out.smtpSsl = parseBool(val);
        }

        return !out.email.empty() && !out.imapHost.empty() && !out.smtpHost.empty();
    }

    bool saveLocalContacts(
            const std::unordered_map<std::string, Contact>& contacts,
            const std::unordered_map<std::string, std::vector<std::string>>& mailingLists)
    {
        if (!ensureCacheDir())
            return false;

        std::ofstream out(getLocalContactsPath().string(), std::ios::trunc);
        if (!out.is_open())
            return false;

        // C|key|email|display|nick
        for (const auto& kv : contacts)
        {
            const Contact& c = kv.second;
            out << "C|" << c.key << "|" << c.email << "|" << c.displayName << "|" << c.nickName << "\n";
        }

        // L|list_name|email1,email2,email3
        for (const auto& kv : mailingLists)
        {
            out << "L|" << kv.first << "|";
            for (size_t i = 0; i < kv.second.size(); ++i)
            {
                if (i != 0)
                    out << ",";
                out << kv.second[i];
            }
            out << "\n";
        }

        return static_cast<bool>(out);
    }

    bool loadLocalContacts(
            std::unordered_map<std::string, Contact>& outContacts,
            std::unordered_map<std::string, std::vector<std::string>>& outMailingLists)
    {
        outContacts.clear();
        outMailingLists.clear();

        std::ifstream in(getLocalContactsPath().string());
        if (!in.is_open())
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            if (line.size() < 3 || line[1] != '|')
                continue;

            const char recType = line[0];
            std::vector<std::string> parts = split(line, '|');

            if (recType == 'C')
            {
                if (parts.size() < 5)
                    continue;

                Contact c;
                c.key = parts[1];
                c.email = parts[2];
                c.displayName = parts[3];
                c.nickName = parts[4];
                if (!c.key.empty())
                    outContacts[c.key] = c;
            }
            else if (recType == 'L')
            {
                if (parts.size() < 3)
                    continue;

                const std::string& listName = parts[1];
                if (listName.empty())
                    continue;
                outMailingLists[listName] = split(parts[2], ',');
            }
        }

        return true;
    }

    bool saveLocalProfileMeta(const LocalProfileMeta& meta)
    {
        if (!ensureCacheDir())
            return false;

        std::ofstream out(getLocalMetaPath().string(), std::ios::trunc);
        if (!out.is_open())
            return false;

        const std::string importedAt = meta.importedAtUtc.empty() ? nowUtcString() : meta.importedAtUtc;
        out << "schema_version=" << meta.schemaVersion << "\n";
        out << "source=" << meta.source << "\n";
        out << "imported_at_utc=" << importedAt << "\n";

        return static_cast<bool>(out);
    }

    bool loadLocalProfileMeta(LocalProfileMeta& out)
    {
        out = LocalProfileMeta{};

        std::ifstream in(getLocalMetaPath().string());
        if (!in.is_open())
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            const auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            const std::string key = trim(line.substr(0, pos));
            const std::string val = trim(line.substr(pos + 1));

            if (key == "schema_version") out.schemaVersion = parseIntOr(val, 1);
            else if (key == "source") out.source = val;
            else if (key == "imported_at_utc") out.importedAtUtc = val;
        }

        if (out.importedAtUtc.empty())
            out.importedAtUtc = nowUtcString();

        return true;
    }
}
