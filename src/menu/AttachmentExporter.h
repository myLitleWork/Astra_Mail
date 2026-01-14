#pragma once

#include <string>
#include <vector>
#include "UtilsFS/FsCompat.h"
#include <cstdint>
#include "src/Resolver/Resolvers/AttachmentGroupsResolver.h"


namespace astra_mail
{

    enum class ExportMode
    {
        BySubject,      // как сейчас (subject/uid_xxx)
        ByExtension     // общие папки по категориям
    };

    struct ExportStrategy
    {
        ExportMode mode;
        bool dedupByHash = true; // актуально для ByExtension
    };

    struct ExportedAttachment
    {
        std::string filename;
        std::vector<std::uint8_t> data;
    };

    class AttachmentExporter
    {
    public:
        AttachmentExporter(
                fs::path outRoot,
                ExportStrategy strategy,
                fs::path attachmentGroupsFile);

        bool loadGroups(); // читает attachment_groups.conf

        // subjectUsed: уже РЕЗОЛВНУТАЯ "маска темы" (после SubjectFolderResolver)
        void exportAttachments(
                const std::string& subjectUsed,
                const std::string& uid,
                const std::vector<ExportedAttachment>& attachments);

    private:
        fs::path outRoot_;
        ExportStrategy strategy_;
        AttachmentGroupsResolver groups_;
        std::string to_;

        void exportBySubject(
                const std::string& subjectUsed,
                const std::string& uid,
                const std::vector<ExportedAttachment>& attachments);

        void exportByExtension(
                const std::vector<ExportedAttachment>& attachments);

        static fs::path makeUnique(const fs::path& dir, fs::path name);

        static std::string fileHash(const fs::path& p);
        static std::string bytesHash(const std::vector<std::uint8_t>& data);

        static std::string normalizeDirUtf8(const std::string& s);
    };

}