#pragma once
#include <string>

namespace astra_mail::config::defaults
{
    const char* subjectRules();
    const char* attachmentGroups();
    const char* outgoingSubjectRules();
    const char* idleSettings();
    const char* networkSettings();
    const char* idleSettingsHelp();
    const char* subjectRulesHelp();
    const char* attachmentGroupsHelp();
    const char* outgoingSubjectRulesHelp();
    const char* tbPrefsLoadError();
    const char* tbNoAccounts();
    const char* tbNoActiveAccount();
    const char* tbNoMailPassword();
    const char* noFlashDrivesFound();
    std::string imapFoldersConfig();
    const char* localMailSettings();
    const char* localContacts();
    const char* localMailSettingsHelp();
    const char* localContactsHelp();
}
