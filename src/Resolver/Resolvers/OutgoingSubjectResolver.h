#pragma once

#include <string>
#include <vector>

#include "UtilsFS/FsCompat.h"

/**
 * Резолвер темы для исходящих писем по именам вложений.
 * Аналог SubjectFolderResolver, но для исходящих: по именам файлов.
 *
 * Правила в файле outgoing_subject_rules.conf:
 *   тема : расширения или prefix:имя
 *
 * Примеры:
 *   ПОДТВЕРЖДЕНИЕ : .tv .tvd prefix:ptdtv
 *   СЛУЖЕБНОЕ : prefix:sluj
 *   ШТ : *
 *
 * Если в письме вложения под разные темы — результат объединяется через пробел.
 */
class OutgoingSubjectResolver
{
public:
    explicit OutgoingSubjectResolver(fs::path rulesFile);

    bool load();

    std::string resolve(const std::vector<std::string>& filenames) const;

    std::string resolveOne(const std::string& filename) const;

private:
    struct Rule
    {
        std::string theme;
        std::vector<std::string> extensions;  // "tv", "tvd"
        std::vector<std::string> prefixes;    // "ptdtv", "sluj"
        bool isDefault = false;
    };

    fs::path rulesFile_;
    std::vector<Rule> rules_;
    std::string fallback_ = "ШТ";

    static std::string trim(const std::string& s);
    static std::string toLowerAscii(std::string s);
    static std::string normalizeExt(const std::string& ext);
};
