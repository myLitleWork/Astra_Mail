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

        out << "# Настройки почтового аккаунта (AstraMail)\n"
            << "# Формат: key=value\n"
            << "#\n"
            << "# profile_id    — идентификатор профиля (папка Thunderbird)\n"
            << "# profile_path  — полный путь к профилю\n"
            << "# account_id    — идентификатор аккаунта\n"
            << "# identity_id   — идентификатор identity\n"
            << "#\n"
            << "# email         — адрес ящика, например: user@mail.ru\n"
            << "# full_name     — имя отправителя (опционально)\n"
            << "# user_name     — логин для IMAP и SMTP\n"
            << "#\n"
            << "# imap_host     — сервер входящей почты, например: imap.mail.ru\n"
            << "# imap_port     — порт IMAP (обычно 993 для SSL, 143 без SSL)\n"
            << "# imap_ssl      — 1 = SSL включен, 0 = выключен\n"
            << "#\n"
            << "# smtp_host     — сервер исходящей почты, например: smtp.mail.ru\n"
            << "# smtp_port     — порт SMTP (обычно 465 для SSL, 25 или 587 без SSL)\n"
            << "# smtp_ssl      — 1 = SSL включен, 0 = выключен\n"
            << "#\n"
            << "# ВАЖНО: замените значения на реальные данные вашего ящика.\n"
            << "#   Значения CHANGE_ME, imap.example.com, smtp.example.com\n"
            << "#   считаются невалидными — программа их не примет.\n"
            << "#\n\n";

        out << "profile_id=" << settings.profileId << "\n";
        out << "profile_path=" << settings.profilePath << "\n";
        out << "account_id=" << settings.accountId << "\n";
        out << "identity_id=" << settings.identityId << "\n";
        out << "\n";
        out << "email=" << settings.email << "\n";
        out << "full_name=" << settings.fullName << "\n";
        out << "user_name=" << settings.userName << "\n";
        out << "\n";
        out << "imap_host=" << settings.imapHost << "\n";
        out << "imap_port=" << settings.imapPort << "\n";
        out << "imap_ssl=" << (settings.imapSsl ? "1" : "0") << "\n";
        out << "\n";
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
            if (line.empty() || line[0] == '#')
                continue;

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

        out << "# Адресная книга (AstraMail)\n"
            << "#\n"
            << "# Контакты:  C|key|email|displayName|nickName\n"
            << "#\n"
            << "# ВАЖНО: nickName ОБЯЗАТЕЛЕН — он используется как имя папки при отправке.\n"
            << "#   Папка \"ivanov\" → контакт с nickName=ivanov → письмо на ivanov@mail.ru\n"
            << "#\n"
            << "# Пример:  C|ivanov|ivanov@mail.ru|Иванов Иван|ivanov\n"
            << "#\n"
            << "# Списки:  L|имя_списка|email1,email2,email3\n"
            << "# Пример:  L|отдел_кадров|ivanov@mail.ru,petrov@yandex.ru\n"
            << "#\n\n";

        for (const auto& kv : contacts)
        {
            const Contact& c = kv.second;
            out << "C|" << c.key << "|" << c.email << "|" << c.displayName << "|" << c.nickName << "\n";
        }

        if (!mailingLists.empty())
            out << "\n";

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
            if (line.empty() || line[0] == '#')
                continue;

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

    bool validateLocalMailSettings(const LocalMailSettings& s, std::string& reason)
    {
        auto isPlaceholderToken = [](const std::string& v)
        {
            return v.find("CHANGE_ME") != std::string::npos;
        };

        if (s.email.empty())
        {
            reason = "email is empty";
            return false;
        }
        if (isPlaceholderToken(s.email))
        {
            reason = "email still contains placeholder (CHANGE_ME)";
            return false;
        }
        if (s.imapHost.empty())
        {
            reason = "imap_host is empty";
            return false;
        }
        if (isPlaceholderToken(s.imapHost) || s.imapHost == "imap.example.com")
        {
            reason = "imap_host still looks like placeholder";
            return false;
        }
        if (s.smtpHost.empty())
        {
            reason = "smtp_host is empty";
            return false;
        }
        if (isPlaceholderToken(s.smtpHost) || s.smtpHost == "smtp.example.com")
        {
            reason = "smtp_host still looks like placeholder";
            return false;
        }
        if (s.userName.empty())
        {
            reason = "user_name is empty";
            return false;
        }
        if (isPlaceholderToken(s.userName))
        {
            reason = "user_name still contains placeholder (CHANGE_ME)";
            return false;
        }
        if (s.imapPort <= 0 || s.imapPort > 65535)
        {
            reason = "imap_port out of range";
            return false;
        }
        if (s.smtpPort <= 0 || s.smtpPort > 65535)
        {
            reason = "smtp_port out of range";
            return false;
        }
        reason.clear();
        return true;
    }

    void clearLocalProfileCache()
    {
        std::error_code ec;
        fs::remove(getLocalMailSettingsPath(), ec);
        fs::remove(getLocalContactsPath(), ec);
        fs::remove(getLocalMetaPath(), ec);
    }

    static bool fileHasContent(const fs::path& path)
    {
        if (!fs::exists(path))
            return false;
        std::error_code ec;
        auto sz = fs::file_size(path, ec);
        return !ec && sz > 10;
    }

    bool ensureLocalMailSettingsFile()
    {
        if (!ensureCacheDir())
            return false;

        const fs::path path = getLocalMailSettingsPath();
        if (fileHasContent(path))
            return true;

        const char* tpl =
                "# Local mail settings (AstraMail cache)\n"
                "# Отредактируйте значения, затем перезапустите программу.\n"
                "# ВАЖНО:\n"
                "#   - значения с токеном CHANGE_ME нужно заменить\n"
                "#   - imap.example.com / smtp.example.com нужно заменить\n"
                "# Иначе кэш будет считаться НЕВАЛИДНЫМ и программа не будет брать его как рабочие настройки.\n"
                "# Формат: key=value\n"
                "#\n"
                "profile_id=\n"
                "profile_path=\n"
                "account_id=local_account_1\n"
                "identity_id=local_identity_1\n"
                "#\n"
                "# Идентичность\n"
                "email=CHANGE_ME@example.com\n"
                "full_name=CHANGE_ME\n"
                "user_name=CHANGE_ME\n"
                "#\n"
                "# IMAP (входящая почта)\n"
                "imap_host=imap.example.com\n"
                "imap_port=993\n"
                "imap_ssl=1\n"
                "#\n"
                "# SMTP (исходящая почта)\n"
                "smtp_host=smtp.example.com\n"
                "smtp_port=465\n"
                "smtp_ssl=1\n";

        std::ofstream out(path.string(), std::ios::trunc);
        if (!out.is_open())
            return false;

        out << tpl;
        return static_cast<bool>(out);
    }

    bool ensureLocalContactsFile()
    {
        if (!ensureCacheDir())
            return false;

        const fs::path path = getLocalContactsPath();
        if (fileHasContent(path))
            return true;

        const char* tpl =
                "# Local contacts (AstraMail cache)\n"
                "# Формат строк:\n"
                "#   C|key|email|displayName|nickName\n"
                "#   L|list_name|email1,email2,email3\n"
                "# key обычно формируется как nickName или displayName или email (если nick/display нет).\n"
                "#\n"
                "# Примеры (закомментированы, не добавятся автоматически):\n"
                "# C|example_key|alice@example.com|Alice Example|alice\n"
                "# L|friends|alice@example.com\n";

        std::ofstream out(path.string(), std::ios::trunc);
        if (!out.is_open())
            return false;

        out << tpl;
        return static_cast<bool>(out);
    }
}
