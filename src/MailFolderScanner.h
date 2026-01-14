#pragma once
#include "UtilsFS/FsCompat.h"

#include <string>
#include "MailFolderModels.h"
#include "Resolver/RecipientResolver.h"

namespace astra_mail
{
    struct MailFolderScanOptions
    {
        bool ignoreHidden = true;        // игнорировать скрытые файлы/папки (начинающиеся с '.')
        bool ignoreEmptyMailFolders = true; // если в папке нет файлов — не считать её "письмом"
        bool allowSubfolders = false;    // пока false: подпапки игнорируем
    };

    class MailFolderScanner
    {
    public:
        explicit MailFolderScanner(MailFolderScanOptions opt, std::shared_ptr<IRecipientResolver> resolver);
        [[nodiscard]] FlashMailScanResult scanMount(const fs::path& mountPoint) const;

        // утилита форматирования размера (для отчёта)
        static std::string formatBytes(uint64_t bytes);

    private:
        MailFolderScanOptions opt_;
        std::shared_ptr<IRecipientResolver> resolver_;

        static bool isHiddenName(const std::string& name);
        static bool looksLikeEmail(const std::string& folderName);

        MailFolderInfo scanSingleFolder(const fs::path& folderPath) const;
    };
}
