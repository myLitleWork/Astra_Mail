#include "AttachmentExporter.h"
#include <fstream>
#include <iostream>
#include <iomanip>


namespace astra_mail
{

    AttachmentExporter::AttachmentExporter(
            fs::path outRoot,
            ExportStrategy strategy,
            fs::path attachmentGroupsFile)
            : outRoot_(std::move(outRoot))
            , strategy_(strategy)
            , groups_(std::move(attachmentGroupsFile))
    {
    }

    bool AttachmentExporter::loadGroups()
    {
        return groups_.load();
    }

// (оставил твою нормализацию максимально простой: не ломаю текущий стиль)
    std::string AttachmentExporter::normalizeDirUtf8(const std::string& s)
    {
        std::string r = s;
        for (char& c : r)
            if (c == '/' || c == '\\') c = '_';
        if (r.empty()) r = "NO_SUBJECT";
        if (r.size() > 64) r.resize(64);
        return r;
    }

    fs::path AttachmentExporter::makeUnique(const fs::path& dir, fs::path name)
    {
        // твоя логика (не трогаю)
        while (fs::exists(dir / name))
        {
            name = name.stem().string() + "-" + name.extension().string();
        }
        return dir / name;
    }

    std::string AttachmentExporter::fileHash(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};

        std::uint64_t h = 1469598103934665603ULL;
        char buf[4096];

        while (f.read(buf, sizeof(buf)) || f.gcount())
        {
            for (std::streamsize i = 0; i < f.gcount(); ++i)
            {
                h ^= (unsigned char)buf[i];
                h *= 1099511628211ULL;
            }
        }
        return std::to_string(h);
    }

    std::string AttachmentExporter::bytesHash(const std::vector<std::uint8_t>& data)
    {
        std::uint64_t h = 1469598103934665603ULL;
        for (auto b : data)
        {
            h ^= (unsigned char)b;
            h *= 1099511628211ULL;
        }
        return std::to_string(h);
    }

    void AttachmentExporter::exportAttachments(
            const std::string& subjectUsed,
            const std::string& uid,
            const std::vector<ExportedAttachment>& attachments)
    {
        if (attachments.empty())
            return;

        switch (strategy_.mode)
        {
            case ExportMode::BySubject:
                exportBySubject(subjectUsed, uid, attachments);
                break;
            case ExportMode::ByExtension:
                exportByExtension(attachments);
                break;
        }
    }

    void AttachmentExporter::exportBySubject(
            const std::string& subjectUsed,
            const std::string& uid,
            const std::vector<ExportedAttachment>& attachments)
    {
        // ВАЖНО: папки создаём только если есть реальное вложение (как ты сделал)
        const std::string subjectDir = normalizeDirUtf8(subjectUsed);

        fs::path baseDir = outRoot_ / subjectDir;
        fs::path mailDir = baseDir / ("uid_" + uid);

        bool dirCreated = false;

        for (const auto& a : attachments)
        {
            if (a.data.empty()) continue;

            if (!dirCreated)
            {
                fs::create_directories(mailDir);
                dirCreated = true;
            }

            fs::path outFile = mailDir / a.filename;
            std::ofstream f(outFile, std::ios::binary);
            if (!f) continue;

            f.write(reinterpret_cast<const char*>(a.data.data()), a.data.size());
            std::cout << "[OK] " << outFile.string()
                      << " (" << a.data.size() << " bytes)\n";
        }
    }

    void AttachmentExporter::exportByExtension(
            const std::vector<ExportedAttachment>& attachments)
    {
        for (const auto& a : attachments)
        {
            if (a.data.empty()) continue;

            const std::string folder = groups_.resolveFolderByFilename(a.filename);
            fs::path dir = outRoot_ / folder;
            fs::create_directories(dir);

            fs::path name(a.filename);
            fs::path dst = dir / name;

            if (strategy_.dedupByHash && fs::exists(dst))
            {
                const auto oldH = fileHash(dst);
                const auto newH = bytesHash(a.data);

                if (!oldH.empty() && oldH == newH)
                    continue; // имя+хеш совпали — пропуск

                dst = makeUnique(dir, name);
            }


            std::ofstream f(dst, std::ios::binary);
            if (!f) continue;

            f.write(reinterpret_cast<const char*>(a.data.data()), a.data.size());
            std::cout << "  ✔ "
                      << std::left << std::setw(25) << a.filename
                      << " (" << a.data.size() << " bytes)\n";

        }
    }

}