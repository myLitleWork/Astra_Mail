#include "AttachmentGroupsResolver.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace astra_mail {

    AttachmentGroupsResolver::AttachmentGroupsResolver(fs::path file)
            : file_(std::move(file))
    {
    }

    std::string AttachmentGroupsResolver::trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        auto b = s.find_first_not_of(ws);
        auto e = s.find_last_not_of(ws);
        if (b == std::string::npos) return {};
        return s.substr(b, e - b + 1);
    }

    std::string AttachmentGroupsResolver::toLowerAscii(std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        return s;
    }

    std::string AttachmentGroupsResolver::normalizeExt(std::string ext)
    {
        ext = trim(ext);
        ext = toLowerAscii(ext);
        if (!ext.empty() && ext[0] != '.') ext.insert(ext.begin(), '.');
        return ext;
    }
    bool AttachmentGroupsResolver::load()
    {
        ext2folder_.clear();
        fallback_ = "Прочее";



        std::ifstream in(file_);
        if (!in)
            return false;

        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;

            // format: Folder : .ext .ext2 OR Folder : *
            auto p = line.find(':');
            if (p == std::string::npos)
                continue;

            std::string folder = trim(line.substr(0, p));
            std::string rest   = trim(line.substr(p + 1));

            if (folder.empty() || rest.empty())
                continue;

            if (rest == "*")
            {
                fallback_ = folder;
                continue;
            }

            std::istringstream iss(rest);
            std::string ext;
            while (iss >> ext)
            {
                ext = normalizeExt(ext);
                if (!ext.empty())
                    ext2folder_[ext] = folder;
            }
        }

        return true;
    }


    std::string AttachmentGroupsResolver::resolveFolderByFilename(const std::string& filename) const
    {
        fs::path p(filename);
        auto ext = normalizeExt(p.extension().string());

        auto it = ext2folder_.find(ext);
        if (it != ext2folder_.end())
            return it->second;

        return fallback_;
    }

}
