#include "CmdViewInboxMails.h"
#include "MailFolderScanner.h"
#include "AstraMailPaths.h"
#include "UtilsFS/FsCompat.h"
#include "src/Utils/Utils.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace astra_mail
{
    struct SubjectEntry
    {
        size_t letters = 0;
        uint64_t totalSizeBytes = 0;
        std::vector<std::pair<std::string, uint64_t>> files;
    };

    static void printInboxReport(std::ostream& os)
    {
        fs::path inboxRoot = getInboxRoot();
        std::error_code ec;
        if (!fs::exists(inboxRoot, ec))
        {
            os << "Папка входящих пуста.\n";
            return;
        }

        std::map<std::string, SubjectEntry> bySubject;
        size_t totalLetters = 0;
        size_t totalAttachments = 0;
        uint64_t totalBytes = 0;

        for (auto it = fs::directory_iterator(inboxRoot, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (ec || !fs::is_directory(it->path()))
                continue;
            std::string subject = it->path().filename().string();
            SubjectEntry& entry = bySubject[subject];

            for (auto uit = fs::directory_iterator(it->path(), ec); !ec && uit != fs::directory_iterator(); uit.increment(ec))
            {
                if (ec || !fs::is_directory(uit->path()))
                    continue;
                std::string name = uit->path().filename().string();
                if (name.size() <= 4 || name.compare(0, 4, "uid_") != 0)
                    continue;

                ++entry.letters;
                ++totalLetters;

                for (auto fit = fs::directory_iterator(uit->path(), ec); !ec && fit != fs::directory_iterator(); fit.increment(ec))
                {
                    if (ec || !fs::is_regular_file(fit->path()))
                        continue;
                    uint64_t sz = 0;
                    auto fsz = fs::file_size(fit->path(), ec);
                    if (!ec) sz = static_cast<uint64_t>(fsz);
                    std::string fname = fit->path().filename().string();
                    entry.files.push_back({fname, sz});
                    entry.totalSizeBytes += sz;
                    ++totalAttachments;
                    totalBytes += sz;
                }
            }
        }

        os << "=== Анализ входящих: " << inboxRoot.string() << " ===\n\n";
        if (bySubject.empty())
        {
            os << "Писем не найдено (папки без файлов игнорируются).\n";
            return;
        }

        size_t idx = 0;
        for (const auto& p : bySubject)
        {
            const SubjectEntry& e = p.second;
            if (e.letters == 0)
                continue;
            ++idx;
            os << "[" << idx << "] Тема: " << p.first << "\n";
            os << "    Папка: " << p.first << "\n";
            os << "    Писем: " << e.letters << ", вложений: " << e.files.size() << "\n";
            os << "    ---------------------------------\n";

            for (const auto& f : e.files)
            {
                os << "      - " << f.first << "    "
                   << MailFolderScanner::formatBytes(f.second) << "\n";
            }

            os << "    ---------------------------------\n";
            os << "    ИТОГО: " << MailFolderScanner::formatBytes(e.totalSizeBytes) << "\n\n";
        }

        os << "-------------------------------------\n";
        os << "Найдено писем: " << totalLetters << "\n";
        os << "Найдено вложений: " << totalAttachments << "\n";
        os << "Общий объём вложений: " << MailFolderScanner::formatBytes(totalBytes) << "\n";
    }

    CommandResult CmdViewInboxMails::execute()
    {
        printInboxReport(std::cout);
        my::pause();
        return CommandResult::Continue;
    }

    CmdViewInboxMails::CmdViewInboxMails() : ICommand() {}

    std::string CmdViewInboxMails::title() const
    {
        return "Просмотр писем во входящих";
    }
}
