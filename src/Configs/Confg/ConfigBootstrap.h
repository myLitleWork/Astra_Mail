// ConfigBootstrap.h
#pragma once
#include "UtilsFS/FsCompat.h"

namespace astra_mail::config
{
    void ensureSubjectRules(bool force = false);
    void ensureAttachmentGroups(bool force = false);
    void ensureOutgoingSubjectRules(bool force = false);
}
