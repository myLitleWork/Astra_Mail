#include "ConfigPaths.h"
#include "UtilsFS/FsUtils.h"

namespace astra_mail::config
{

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
}
