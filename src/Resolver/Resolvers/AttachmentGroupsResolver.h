#pragma once
#include <string>
#include <unordered_map>
#include "Resolver/RecipientResolver.h"
#include "UtilsFS/FsCompat.h"


namespace astra_mail
{

    class AttachmentGroupsResolver
    {
    public:
        explicit AttachmentGroupsResolver(fs::path file);

        bool load();
        std::string resolveFolderByFilename(const std::string& filename) const;

    private:
        fs::path file_;
        std::unordered_map<std::string, std::string> ext2folder_; // ".pdf"->"Приложения"
        std::string fallback_ = "Прочее";

    private:
        static std::string trim(const std::string& s);
        static std::string toLowerAscii(std::string s);
        static std::string normalizeExt(std::string ext); // ".PDF" -> ".pdf"
    };

}