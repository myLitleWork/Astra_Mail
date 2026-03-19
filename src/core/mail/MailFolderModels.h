#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace astra_mail
{
    struct AttachmentInfo
    {
        std::string name;     // имя файла без пути
        uint64_t    sizeBytes = 0;
        std::string fullPath;
    };

    struct MailFolderInfo
    {
        std::string folderPath;
        std::string folderName;   // имя папки в корне флешки
        std::string recipient;    // email или alias (пока как есть)
        bool        isAlias = false;

        std::vector<AttachmentInfo> attachments;
        uint64_t totalSizeBytes = 0;
    };

    struct FlashMailScanResult
    {
        std::string mountPoint;
        std::vector<MailFolderInfo> mails;

        uint64_t totalSizeBytes = 0;
        size_t totalMails = 0;
        size_t totalAttachments = 0;
    };
}
