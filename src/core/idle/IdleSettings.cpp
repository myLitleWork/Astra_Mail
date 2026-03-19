#include "IdleSettings.h"
#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "UtilsFS/FsCompat.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

namespace astra_mail
{
    static uint32_t parsePositiveU32(const std::string& s, uint32_t def)
    {
        if (s.empty())
            return def;
        char* end = nullptr;
        unsigned long v = std::strtoul(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0' || v == 0 || v > 86400) // max 24h
            return def;
        return static_cast<uint32_t>(v);
    }

    IdleSettingsValues loadIdleSettings()
    {
        IdleSettingsValues out;
        config::ensureIdleSettings(false);

        fs::path path = config::idleSettings();
        std::ifstream f(path);
        if (!f)
            return out;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                val.erase(0, 1);

            if (key == "idle_threshold_seconds")
                out.idleThresholdSeconds = parsePositiveU32(val, 300);
            else if (key == "check_interval_seconds")
                out.checkIntervalSeconds = parsePositiveU32(val, 30);
        }
        return out;
    }
}
