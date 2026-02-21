#include "OutgoingSubjectResolver.h"
#include "UtilsFS/FsCompat.h"
#include <fstream>
#include <sstream>
#include <set>

OutgoingSubjectResolver::OutgoingSubjectResolver(fs::path rulesFile)
    : rulesFile_(std::move(rulesFile))
{
}

std::string OutgoingSubjectResolver::trim(const std::string& s)
{
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    auto e = s.find_last_not_of(ws);
    if (b == std::string::npos)
        return {};
    return s.substr(b, e - b + 1);
}

std::string OutgoingSubjectResolver::toLowerAscii(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'A' && c <= 'Z')
        {
            s[i] = static_cast<char>(c - 'A' + 'a');
        }
        else if (i + 1 < s.size() && c == 0xD0)
        {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            // Кириллица А-П (UTF-8 0xD0 0x90–0x9F) → а-п (0xD0 0xB0–0xBF)
            if (c1 >= 0x90 && c1 <= 0x9F)
            {
                s[i + 1] = static_cast<char>(c1 + 0x20);
                ++i;
            }
            // Кириллица Р-Я (UTF-8 0xD0 0xA0–0xAF) → р-я (0xD1 0x80–0x8F)
            else if (c1 >= 0xA0 && c1 <= 0xAF)
            {
                s[i] = static_cast<char>(0xD1);
                s[i + 1] = static_cast<char>(c1 - 0x20);
                ++i;
            }
        }
    }
    return s;
}

std::string OutgoingSubjectResolver::normalizeExt(const std::string& ext)
{
    std::string e = trim(ext);
    while (!e.empty() && e[0] == '.')
        e.erase(0, 1);
    return toLowerAscii(e);
}

bool OutgoingSubjectResolver::load()
{
    rules_.clear();
    fallback_ = "ШТ";

    std::ifstream in(rulesFile_);
    if (!in)
        return false;

    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        auto p = line.find(':');
        if (p == std::string::npos)
            continue;

        std::string theme = trim(line.substr(0, p));
        std::string rest  = trim(line.substr(p + 1));

        if (theme.empty() || rest.empty())
            continue;

        Rule r;
        r.theme = theme;

        if (rest == "*")
        {
            r.isDefault = true;
            rules_.push_back(std::move(r));
            continue;
        }

        std::istringstream iss(rest);
        std::string token;
        while (iss >> token)
        {
            token = trim(token);
            if (token.empty())
                continue;

            if (token.size() > 7 && token.compare(0, 7, "prefix:") == 0)
            {
                std::string prefix = toLowerAscii(token.substr(7));
                if (!prefix.empty())
                    r.prefixes.push_back(std::move(prefix));
            }
            else
            {
                std::string ext = normalizeExt(token);
                if (!ext.empty())
                    r.extensions.push_back(std::move(ext));
            }
        }

        rules_.push_back(std::move(r));
    }

    return !rules_.empty();
}

std::string OutgoingSubjectResolver::resolveOne(const std::string& filename) const
{
    if (filename.empty())
        return fallback_;

    fs::path p(filename);
    std::string ext  = normalizeExt(p.extension().string());
    std::string stem = toLowerAscii(p.stem().string());

    for (const auto& r : rules_)
    {
        if (r.isDefault)
            return r.theme;

        for (const auto& e : r.extensions)
        {
            if (ext == e)
                return r.theme;
        }

        for (const auto& pref : r.prefixes)
        {
            if (stem.size() >= pref.size() &&
                stem.compare(0, pref.size(), pref) == 0)
                return r.theme;
        }
    }

    return fallback_;
}

std::string OutgoingSubjectResolver::resolve(const std::vector<std::string>& filenames) const
{
    if (filenames.empty())
        return fallback_;

    std::set<std::string> themes;
    for (const auto& f : filenames)
    {
        std::string t = resolveOne(f);
        if (!t.empty())
            themes.insert(t);
    }

    if (themes.empty())
        return fallback_;

    // Порядок: в порядке появления в rules_
    std::string result;
    for (const auto& r : rules_)
    {
        if (themes.count(r.theme))
        {
            if (!result.empty())
                result += ' ';
            result += r.theme;
        }
    }
    // Темы, не попавшие в rules_ (например fallback)
    for (const auto& t : themes)
    {
        bool found = false;
        for (const auto& r : rules_)
            if (r.theme == t) { found = true; break; }
        if (!found)
        {
            if (!result.empty())
                result += ' ';
            result += t;
        }
    }

    return result.empty() ? fallback_ : result;
}
