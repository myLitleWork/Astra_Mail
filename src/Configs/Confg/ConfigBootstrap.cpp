// ConfigBootstrap.cpp
#include "ConfigBootstrap.h"
#include "ConfigPaths.h"
#include "ConfigDefaults.h"
#include <fstream>
#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace astra_mail::config
{
    static void fileError(const fs::path& path, const char* operation,
                          const std::error_code& ec = {})
    {
        throw std::runtime_error("Конфигурация '" + path.string() + "': " + operation +
                                 (ec ? ": " + ec.message() : ""));
    }

    static void ensureFile(const fs::path& path, const char* content, bool force)
    {
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (ec) fileError(path, "не удалось проверить файл", ec);
        if (exists)
        {
            const bool regular = fs::is_regular_file(path, ec);
            if (ec) fileError(path, "не удалось проверить тип файла", ec);
            if (!regular) fileError(path, "ожидался обычный файл");
            if (!force)
            {
                const auto size = fs::file_size(path, ec);
                if (ec) fileError(path, "не удалось определить размер файла", ec);
                if (size > 0) return;
            }
        }

        if (!path.parent_path().empty())
        {
            fs::create_directories(path.parent_path(), ec);
            if (ec) fileError(path, "не удалось создать каталог настроек", ec);
        }

        errno = 0;
        std::ofstream out(path.string());
        if (!out) fileError(path, "не удалось открыть файл для записи; проверьте права доступа",
                            std::error_code(errno, std::generic_category()));
        errno = 0;
        out << content;
        out.close();
        if (!out) fileError(path, "не удалось записать файл; проверьте свободное место и права доступа",
                            std::error_code(errno, std::generic_category()));
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
        ensureFile(localMailSettings(), defaults::localMailSettings(), force);
    }

    void ensureNetworkSettings(bool force)
    {
        ensureFile(networkSettings(), defaults::networkSettings(), force);
    }

    void ensureLocalContacts(bool force)
    {
        ensureFile(localContacts(), defaults::localContacts(), force);
    }
}
