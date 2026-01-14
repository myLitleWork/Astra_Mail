#pragma once
#include <string>

namespace astra_mail::imap {

    class FolderFilter
    {
    public:
        explicit FolderFilter(const std::string& configPath);

        void load();
        bool shouldShow(const std::string& folder) const;

    private:
        std::string configPath_;
    };

}
