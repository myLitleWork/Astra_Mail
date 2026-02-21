#pragma once

#include <string>
#include <vector>
#include <regex>

#include "UtilsFS/FsCompat.h"

class SubjectFolderResolver
{
public:
    explicit SubjectFolderResolver(fs::path  rulesFile);

    // загрузка / перезагрузка
    bool load();

    // определить папку по теме письма
    std::string resolve(const std::string& subject) const;

private:
    enum class RuleType
    {
        Regex,
        WRegex,
        Default
    };

    struct Rule
    {
        RuleType type;
        std::string folder;
        std::regex re;
        std::wregex wre;
    };

    fs::path rulesFile_;
    std::vector<Rule> rules_;

private:
    static std::string trim(const std::string& s);
    static std::wstring utf8ToWstring(const std::string& s);
};
