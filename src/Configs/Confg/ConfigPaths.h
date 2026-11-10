#pragma once
#include <unistd.h>
#include <cstdlib>
#include "UtilsFS/FsCompat.h"

namespace astra_mail::config
{
    fs::path subjectRules();
    fs::path attachmentGroups();
    fs::path outgoingSubjectRules();
    fs::path idleSettings();
    fs::path networkSettings();
    std::string imapFoldersConfig();
    fs::path localMailSettings();
    fs::path localContacts();
}
