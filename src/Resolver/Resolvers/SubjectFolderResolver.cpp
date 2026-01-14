#include "SubjectFolderResolver.h"

#include <fstream>
#include <codecvt>
#include <locale>
#include <utility>

std::string SubjectFolderResolver::trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    auto e = s.find_last_not_of(ws);
    if (b == std::string::npos)
        return {};
    return s.substr(b, e - b + 1);
}

std::wstring SubjectFolderResolver::utf8ToWstring(const std::string& s)
{
    static std::wstring_convert<
            std::codecvt_utf8<wchar_t>
    > conv;
    return conv.from_bytes(s);
}


SubjectFolderResolver::SubjectFolderResolver(fs::path  rulesFile)
        : rulesFile_(std::move(rulesFile))
{
}

bool SubjectFolderResolver::load()
{
    rules_.clear();

    std::ifstream in(rulesFile_);
    if (!in)
        return false;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        auto p1 = line.find('|');
        auto p2 = line.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos)
            continue;

        std::string folder = trim(line.substr(0, p1));
        std::string type   = trim(line.substr(p1 + 1, p2 - p1 - 1));
        std::string patt   = trim(line.substr(p2 + 1));

        Rule r;
        r.folder = folder;

        try
        {
            if (type == "regex")
            {
                r.type = RuleType::Regex;
                r.re = std::regex(patt, std::regex::icase);
            }
            else if (type == "wregex")
            {
                r.type = RuleType::WRegex;
                r.wre = std::wregex(utf8ToWstring(patt),
                                    std::regex::icase);
            }
            else if (type == "default")
            {
                r.type = RuleType::Default;
            }
            else
                continue;
        }
        catch (...)
        {
            // битое правило — просто пропускаем
            continue;
        }

        rules_.push_back(std::move(r));
    }

    return !rules_.empty();
}

std::string SubjectFolderResolver::resolve(const std::string& subject) const
{
    std::wstring subjectW;

    for (const auto& r : rules_)
    {
        switch (r.type)
        {
            case RuleType::Regex:
                if (std::regex_search(subject, r.re))
                    return r.folder;
                break;

            case RuleType::WRegex:
                if (subjectW.empty())
                    subjectW = utf8ToWstring(subject);
                if (std::regex_search(subjectW, r.wre))
                    return r.folder;
                break;

            case RuleType::Default:
                return r.folder;
        }
    }

    return "Прочее";
}
