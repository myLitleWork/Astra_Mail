#include "MailFolderScanner.h"
#include <sstream>
#include <iomanip>





namespace astra_mail
{
    MailFolderScanner::MailFolderScanner(
            MailFolderScanOptions opt,
            std::shared_ptr<IRecipientResolver> resolver)
            : opt_(opt), resolver_(std::move(resolver))
    {}


    bool MailFolderScanner::isHiddenName(const std::string& name)
    {
        return !name.empty() && name[0] == '.';
    }

    bool MailFolderScanner::looksLikeEmail(const std::string& folderName)
    {
        // Минимальная эвристика: есть '@' и хотя бы одна точка после '@'
        auto at = folderName.find('@');
        if (at == std::string::npos) return false;

        auto dot = folderName.find('.', at + 1);
        return dot != std::string::npos && dot > at + 1 && dot + 1 < folderName.size();
    }

    MailFolderInfo MailFolderScanner::scanSingleFolder(const fs::path& folderPath) const
    {

        MailFolderInfo info;
        info.folderName = folderPath.filename().string();
        info.recipient  = info.folderName;
        info.folderPath = folderPath.string();   // ✅ ВОТ ЭТО ОБЯЗАТЕЛЬНО
        info.isAlias    = !looksLikeEmail(info.folderName);

        std::error_code ec;

        for (auto it = fs::directory_iterator(folderPath, ec);
             !ec && it != fs::end(it);
             it.increment(ec))
        {
            const auto& entry = *it;
            auto name = entry.path().filename().string();
            auto path = entry.path().string();

            if (opt_.ignoreHidden && isHiddenName(name))
                continue;

            if (fs::is_regular_file(entry,ec))
            {
                uint64_t sz = 0;
                auto fsz = fs::file_size(entry,ec);
                if (!ec) sz = static_cast<uint64_t>(fsz);

                info.attachments.push_back(AttachmentInfo{ name, sz , path  });
                info.totalSizeBytes += sz;
            }
            else if (fs::is_directory(entry,ec))
            {
                // подпапки пока игнорируем (opt_.allowSubfolders = false)
                // если позже захочешь — здесь можно включить рекурсию.
                (void)0;
            }
        }

        return info;
    }

    FlashMailScanResult MailFolderScanner::scanMount(const fs::path& mountPoint) const
    {
        FlashMailScanResult result;
        result.mountPoint = mountPoint.string();

        std::error_code ec;

        if (!fs::exists(mountPoint, ec) || ec)
            return result;

        for (auto it = fs::directory_iterator(mountPoint, ec);
             !ec && it != fs::end(it);
             it.increment(ec))
        {
            const auto& entry = *it;
            auto name = entry.path().filename().string();

            if (opt_.ignoreHidden && isHiddenName(name))
                continue;

            if (!fs::is_directory(entry,ec))
                continue;

            auto resolved = resolver_->resolve(name);
            if (!resolved)
                continue; // ❗ ФИЛЬТР: папка невалидна

            auto mail = scanSingleFolder(entry.path());

            mail.recipient = *resolved;
            mail.isAlias = (mail.folderName != *resolved);

            if (opt_.ignoreEmptyMailFolders && mail.attachments.empty())
                continue;

            result.totalSizeBytes += mail.totalSizeBytes;
            result.totalAttachments += mail.attachments.size();
            result.mails.push_back(std::move(mail));
        }

        result.totalMails = result.mails.size();
        return result;
    }

    std::string MailFolderScanner::formatBytes(uint64_t bytes)
    {
        static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        auto value = static_cast<double>(bytes);
        int u = 0;
        while (value >= 1024.0 && u < 4) { value /= 1024.0; ++u; }

        std::ostringstream ss;
        ss << std::fixed << std::setprecision((u == 0) ? 0 : 2) << value << " " << units[u];
        return ss.str();
    }
}
