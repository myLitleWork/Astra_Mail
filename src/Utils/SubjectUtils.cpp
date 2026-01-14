#include "SubjectUtils.h"
#include <cctype>

std::string normalizeSubjectToDir(const std::string& subject)
{
    std::string out;
    out.reserve(subject.size());

    for (unsigned char uc : subject)
    {
        char c = static_cast<char>(uc);
        if (std::isalnum(uc) || c == '_' || c == '-')
            out.push_back(c);
        else if (std::isspace(c))
            out.push_back('_');
    }

    if (out.empty())
        out = "NO_SUBJECT";

    if (out.size() > 64)
        out.resize(64);

    return out;
}
