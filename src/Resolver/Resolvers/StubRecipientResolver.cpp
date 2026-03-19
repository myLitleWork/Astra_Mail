#include "StubRecipientResolver.h"


namespace astra_mail
{
    static bool looksLikeEmail(const std::string& s)
    {
        auto at = s.find('@');
        if (at == std::string::npos) return false;
        auto dot = s.find('.', at + 1);
        return dot != std::string::npos && dot + 1 < s.size();
    }

    optional<std::string>
    StubRecipientResolver::resolve(const std::string& folderName) const
    {

        if (folderName == "System Volume Information" ||
            folderName == "$RECYCLE.BIN" ||
            folderName == "RECYCLER" ||
            folderName == "Recycler")
        {
            return optional<std::string>{};
        }

        if (looksLikeEmail(folderName))
            return folderName;

        return folderName + "@dvkr.net";
    }

}
