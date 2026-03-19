// ConfigBootstrap.cpp
#include "ConfigBootstrap.h"
#include "ConfigPaths.h"
#include "ConfigDefaults.h"
#include <fstream>

namespace astra_mail::config
{
    void ensureFile(const fs::path& path, const char* content, bool force)
    {
        if (!force && fs::exists(path))
        {
            std::error_code ec;
            auto sz = fs::file_size(path, ec);
            if (!ec && sz > 0)
                return;
        }

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

    void ensureOutgoingSubjectRules(bool force)
    {
        ensureFile(outgoingSubjectRules(), defaults::outgoingSubjectRules(), force);
    }

    void ensureIdleSettings(bool force)
    {
        ensureFile(idleSettings(), defaults::idleSettings(), force);
    }

    void ensureLocalMailSettings(bool force)
    {
        std::error_code ec;
        fs::create_directories(localMailSettings().parent_path(), ec);
        ensureFile(localMailSettings(), defaults::localMailSettings(), force);
    }

    void ensureLocalContacts(bool force)
    {
        std::error_code ec;
        fs::create_directories(localContacts().parent_path(), ec);
        ensureFile(localContacts(), defaults::localContacts(), force);
    }
}
