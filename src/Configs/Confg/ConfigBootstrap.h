// ConfigBootstrap.h
#pragma once
#include "UtilsFS/FsCompat.h"

namespace astra_mail::config
{
    // Missing/empty files receive defaults; nonempty files are preserved unless force=true.
    // Throws std::runtime_error with the path and operation on filesystem/write errors.
    void ensureSubjectRules(bool force = false);
    void ensureAttachmentGroups(bool force = false);
    void ensureOutgoingSubjectRules(bool force = false);
    void ensureIdleSettings(bool force = false);
    void ensureNetworkSettings(bool force = false);
    void ensureLocalMailSettings(bool force = false);
    void ensureLocalContacts(bool force = false);
}
