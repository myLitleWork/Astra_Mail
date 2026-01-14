// ConfigBootstrap.cpp
#include "ConfigBootstrap.h"
#include "ConfigPaths.h"
#include "ConfigDefaults.h"
#include <fstream>

namespace astra_mail::config
{
    void ensureFile(const fs::path& path, const char* content, bool force)
    {
        if (fs::exists(path) && !force)
            return;

        std::ofstream out(path);
        if (out)
            out << content;
    }

    void ensureSubjectRules(bool force)
    {
        ensureFile(subjectRules(), defaults::subjectRules(), force);
    }

    void ensureAttachmentGroups(bool force)
    {
        ensureFile(attachmentGroups(), defaults::attachmentGroups(), force);
    }
}
