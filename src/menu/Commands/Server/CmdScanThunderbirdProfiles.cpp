#include "CmdScanThunderbirdProfiles.h"

#include "Utils/Utils.h"

#include "UtilsFS/FsCompat.h"

#include <fstream>
#include <iostream>
#include <map>




namespace astra_mail {


    static std::string trim(std::string s)
    {
        while (!s.empty() && (s.back()=='\r' || s.back()=='\n' || s.back()==' ' || s.back()=='\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i]==' ' || s[i]=='\t')) ++i;
        return s.substr(i);
    }

    static bool startsWith(const std::string& s, const char* pfx)
    {
        return s.rfind(pfx, 0) == 0;
    }

    static std::string stripQuotes(std::string s)
    {
        s = trim(std::move(s));
        if (s.size() >= 2 && ((s.front()=='"' && s.back()=='"') || (s.front()=='\'' && s.back()=='\'')))
            return s.substr(1, s.size()-2);
        return s;
    }

// простой ini-парсер: читает только key=value и [section]
    using IniMap = std::map<std::string, std::map<std::string, std::string>>;

    static IniMap readIni(const fs::path& p)
    {
        IniMap out;
        if (!fs::exists(p)) return out;

        std::ifstream in(p.string());
        if (!in) return out;

        std::string section;
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty()) continue;
            if (line[0] == ';' || line[0] == '#') continue;

            if (line.front()=='[' && line.back()==']')
            {
                section = line.substr(1, line.size()-2);
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = stripQuotes(line.substr(eq+1));
            out[section][key] = val;
        }
        return out;
    }

// compatibility.ini: [Compatibility] LastVersion=115.4.1_...
    static std::string readTbVersionFromCompatibility(const fs::path& profileDir)
    {
        auto comp = profileDir / "compatibility.ini";
        if (!fs::exists(comp)) return {};

        auto ini = readIni(comp);
        auto itS = ini.find("Compatibility");
        if (itS == ini.end()) return {};

        auto itK = itS->second.find("LastVersion");
        if (itK == itS->second.end()) return {};

        std::string v = itK->second;           // "115.4.1_2023....../..."
        auto pos = v.find('_');
        if (pos != std::string::npos) v = v.substr(0, pos);
        pos = v.find('/');
        if (pos != std::string::npos) v = v.substr(0, pos);
        return v;
    }

    static std::time_t getMTime(const fs::path& p)
    {
        return decltype(fs::last_write_time(p))::clock::to_time_t(
                fs::last_write_time(p)
        );
    }

    static std::time_t scanMailActivity(const fs::path& imapDir)
    {
        std::time_t latest = 0;

        for (const auto& acc : fs::directory_iterator(imapDir))
        {
            if (!fs::is_directory(acc)) continue;

            for (const auto& f : fs::directory_iterator(acc))
            {
                auto name = f.path().filename().string();
                if (name == "INBOX" || name == "Sent" || name == "Sent Messages")
                {
                    latest = std::max(latest, getMTime(f.path()));
                }
            }
        }
        return latest;
    }


    std::string CmdScanThunderbirdProfiles::title() const
    {
        return "Найти профили Thunderbird";
    }

    std::vector<TbProfile>& CmdScanThunderbirdProfiles::profiles()
    {
        return g_profiles;
    }


    CommandResult CmdScanThunderbirdProfiles::execute()
    {
        g_profiles.clear();

        fs::path base = fs::path(getenv("HOME")) / ".thunderbird";

        if (!fs::exists(base))
        {
            std::cout << "Каталог ~/.thunderbird не найден\n";
            my::pause();
            return CommandResult::Continue;
        }

        // 1) читаем profiles.ini — это главный источник "Name/Path/Default"
        IniMap profilesIni = readIni(base / "profiles.ini");

        // 2) читаем installs.ini — чтобы понять default для конкретной установки
        //    (на машине может быть несколько установок/версий)
        IniMap installsIni = readIni(base / "installs.ini");

        // Соберём mapping: profilePath (как в ini) -> installId (где он Default)
        std::map<std::string, std::string> defaultByInstall; // "76bsnx3z.default-release" -> "B4DB..."
        for (const auto& sec : installsIni)
        {
            // секция вида [B4DBEB87AF7D91B7]
            const std::string& installId = sec.first;
            auto itDef = sec.second.find("Default");
            if (itDef == sec.second.end()) continue;
            defaultByInstall[itDef->second] = installId;
        }

        // 3) Обходим секции ProfileX из profiles.ini
        for (const auto& sec : profilesIni)
        {
            if (!startsWith(sec.first, "Profile")) continue;

            const auto& m = sec.second;

            auto itPath = m.find("Path");
            if (itPath == m.end()) continue;

            std::string relPath = itPath->second; // "kv9gppx5.default-release-1"
            bool isRel = true;
            auto itRel = m.find("IsRelative");
            if (itRel != m.end()) isRel = (itRel->second == "1");

            fs::path profileDir = isRel ? (base / relPath) : fs::path(relPath);

            // prefs.js обязателен, иначе это не tb-профиль
            fs::path prefs = profileDir / "prefs.js";
            if (!fs::exists(prefs)) continue;

            TbProfile p;
            p.id   = profileDir.filename().string(); // папка
            p.path = profileDir.string();

            // Name из ini (человекочитаемое)
            auto itName = m.find("Name");
            if (itName != m.end()) p.name = itName->second;
            else p.name = p.id;

            // Default=1 (profiles.ini)
            auto itDef = m.find("Default");
            if (itDef != m.end() && itDef->second == "1")
                p.isDefaultProfile = true;

            // installs.ini Default=... (default для установки)
            auto itInstall = defaultByInstall.find(relPath);
            if (itInstall != defaultByInstall.end())
            {
                p.isDefaultForInstall = true;
                p.installId = itInstall->second;
            }

            // время изменений
            p.lastModified = getMTime(prefs);

            // активность почты
            fs::path imap = profileDir / "ImapMail";
            if (fs::exists(imap) && fs::is_directory(imap))
                p.lastMailActivity = scanMailActivity(imap);

            // версия TB
            p.tbVersion = readTbVersionFromCompatibility(profileDir);

            g_profiles.push_back(std::move(p));
        }

        return CommandResult::Continue;
    }


    std::vector<TbProfile> CmdScanThunderbirdProfiles::getProfiles() {
        CmdScanThunderbirdProfiles scan;
        scan.execute();
        return scan.profiles();
    }

}
