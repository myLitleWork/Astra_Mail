#include "ConfigPaths.h"
#include "UtilsFS/FsUtils.h"
#include <cstdlib>

namespace astra_mail::config
{
    static fs::path profileCacheDir()
    {
        const char* home = std::getenv("HOME");
        fs::path base = (home && *home) ? fs::path(home) : fs::current_path();
        return base / ".astramail" / "profile_cache";
    }


    fs::path subjectRules()
    {
        return fsu::getExecutableDir() / fs::path{"subject_rules.conf"};
    }
    std::string imapFoldersConfig()
    {
        return fsu::getExecutableDir() / fs::path{"imap_folders.conf"};
    }
    fs::path attachmentGroups()
    {
        return fsu::getExecutableDir() / fs::path{"attachment_groups.conf"};
    }

    fs::path outgoingSubjectRules()
    {
        return fsu::getExecutableDir() / fs::path{"outgoing_subject_rules.conf"};
    }

    fs::path idleSettings()
    {
        return fsu::getExecutableDir() / fs::path{"idle_settings.conf"};
    }

    fs::path networkSettings()
    {
        return fsu::getExecutableDir() / fs::path{"network_settings.conf"};
    }

    fs::path localMailSettings()
    {
        return profileCacheDir() / "mail_settings.conf";
    }

    fs::path localContacts()
    {
        return profileCacheDir() / "contacts.conf";
    }
}
