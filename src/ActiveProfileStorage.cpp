#include "ActiveProfileStorage.h"

#include <fstream>
#include <iostream>
#include <cstdlib>   // getenv
#include <cerrno>
#include <cstring>   // strerror
#include "UtilsFS/FsCompat.h"


namespace astra_mail
{
    static fs::path getActiveProfileFilePath()
    {
        const char* home = std::getenv("HOME");
        fs::path base = home && *home ? fs::path(home) : fs::current_path();

        // однозначно: ~/.astramail/active_profile.conf
        return base / ".astramail" / "active_profile.conf";
    }

    bool saveActiveProfile(const TbProfile& profile)
    {
        if (profile.id.empty() || profile.path.empty())
        {
            std::cout << "[SAVE][ERROR] profile is empty (id/path)\n";
            return false;
        }

        const fs::path filePath = getActiveProfileFilePath();
        const fs::path dirPath  = filePath.parent_path();

        std::error_code ec;
        fs::create_directories(dirPath, ec);
        if (ec)
        {
            std::cout << "[SAVE][ERROR] create_directories failed: "
                      << dirPath.string() << " : " << ec.message() << "\n";
            return false;
        }

        std::cout << "[SAVE] writing to: " << filePath.string() << "\n";

        std::ofstream f(filePath.string(), std::ios::out | std::ios::trunc);
        if (!f)
        {
            std::cout << "[SAVE][ERROR] cannot open file: " << filePath.string()
                      << " errno=" << errno << " (" << std::strerror(errno) << ")\n";
            return false;
        }

        // простой формат key=value
        f << "profile_id="        << profile.id << "\n";
        f << "profile_path="      << profile.path << "\n";
        f << "active_account_id=" << profile.active_account_id << "\n";


        f.flush();
        if (!f)
        {
            std::cout << "[SAVE][ERROR] flush failed: errno=" << errno
                      << " (" << std::strerror(errno) << ")\n";
            return false;
        }

        f.close();

        // контроль: файл реально появился и не пустой
        std::error_code ec2;
        auto sz = fs::file_size(filePath, ec2);
        if (ec2)
        {
            std::cout << "[SAVE][WARN] file_size failed: " << ec2.message() << "\n";
        }
        else
        {
            std::cout << "[SAVE] ok, size=" << sz << "\n";
        }

        return true;
    }

    bool loadActiveProfile(TbProfile& out)
    {
        out = TbProfile{};

        const fs::path filePath = getActiveProfileFilePath();
        std::cout << "[LOAD] reading from: " << filePath.string() << "\n";

        std::ifstream f(filePath.string());
        if (!f)
        {
            std::cout << "[LOAD][WARN] file not found/open failed: " << filePath.string()
                      << " errno=" << errno << " (" << std::strerror(errno) << ")\n";
            return false;
        }

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty()) continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);

            if (key == "profile_id") out.id = val;
            else if (key == "profile_path") out.path = val;
            else if (key == "active_account_id") out.active_account_id = val;

        }

        const bool ok = !out.id.empty() && !out.path.empty();
        std::cout << "[LOAD] ok=" << (ok ? "YES" : "NO")
                  << " id=[" << out.id << "] path=[" << out.path << "]\n";
        return ok;
    }

} // namespace astra_mail
