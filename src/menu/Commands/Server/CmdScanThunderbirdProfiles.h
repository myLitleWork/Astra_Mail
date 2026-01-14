#pragma once
#include "menu/Command.h"
#include <vector>
#include <ctime>

namespace astra_mail {

    struct TbProfile {
        // папка профиля (то что сейчас показываешь)
        std::string id;        // например "kv9gppx5.default-release-1"
        std::string active_account_id;
        // красивое имя из profiles.ini: Name=...
        std::string name;      // например "default-release-1"

        // полный путь к папке профиля
        std::string path;

        // версия Thunderbird по compatibility.ini (LastVersion)
        std::string tbVersion; // например "115.4.1" или "78.10.2"

        // default из profiles.ini (Default=1)
        bool isDefaultProfile = false;

        // default профиля для конкретной установки из installs.ini
        bool isDefaultForInstall = false;
        std::string installId; // например "B4DBEB87AF7D91B7"

        std::time_t lastModified = 0;
        std::time_t lastMailActivity = 0;
    };



    class CmdScanThunderbirdProfiles : public ICommand {

        std::vector<TbProfile> g_profiles;

    public:
        std::string title() const override;
        CommandResult execute() override;
        static std::vector<TbProfile> getProfiles();

        std::vector<TbProfile>& profiles();
    };

}
