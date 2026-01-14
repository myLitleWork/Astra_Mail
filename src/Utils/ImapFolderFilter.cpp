#include "ImapFolderFilter.h"
#include <fstream>
#include <unordered_set>
#include "UtilsFS/FsCompat.h"
#include "src/Configs/Confg/ConfigDefaults.h"

namespace astra_mail::imap {

    static std::unordered_set<std::string> hidden;

    FolderFilter::FolderFilter(const std::string& path)
            : configPath_(path)
    {}

    void FolderFilter::load()
    {
        hidden.clear();

        if (!fs::exists(configPath_))
        {
            fs::create_directories(fs::path(configPath_).parent_path());

            std::ofstream out(configPath_);
            out << config::defaults::imapFoldersConfig();
            out.close();
        }

        std::ifstream in(configPath_);
        std::string line;

        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);

            if (key == "hide" && !val.empty())
                hidden.insert(val);
        }
    }

    bool FolderFilter::shouldShow(const std::string& folder) const
    {
        return hidden.find(folder) == hidden.end();
    }

}
