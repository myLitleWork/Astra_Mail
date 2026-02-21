#include "CmdViewInboxMails.h"
#include "MailFolderScanner.h"
#include "AstraMailPaths.h"
#include "UtilsFS/FsCompat.h"
#include "src/Utils/Utils.h"
#include <iostream>
#include <map>
#include <string>

namespace astra_mail
{
    static void printInboxReport(std::ostream& os)
    {
        fs::path inboxRoot = getInboxRoot();
        std::error_code ec;
        if (!fs::exists(inboxRoot, ec))
        {
            os << "Папка входящих пуста.\n";
            return;
        }

        struct SubjectStats { size_t letters = 0; size_t attachments = 0; uint64_t sizeBytes = 0; };
        std::map<std::string, SubjectStats> bySubject;
        size_t totalLetters = 0;
        size_t totalAttachments = 0;
        uint64_t totalBytes = 0;

        for (auto it = fs::directory_iterator(inboxRoot, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        {
            if (ec || !fs::is_directory(it->path()))
                continue;
            std::string subject = it->path().filename().string();
            auto& stats = bySubject[subject];

            for (auto uit = fs::directory_iterator(it->path(), ec); !ec && uit != fs::directory_iterator(); uit.increment(ec))
            {
                if (ec || !fs::is_directory(uit->path()))
                    continue;
                std::string name = uit->path().filename().string();
                if (name.size() <= 4 || name.compare(0, 4, "uid_") != 0)
                    continue;

                ++stats.letters;
                ++totalLetters;

                for (auto fit = fs::directory_iterator(uit->path(), ec); !ec && fit != fs::directory_iterator(); fit.increment(ec))
                {
                    if (ec || !fs::is_regular_file(fit->path()))
                        continue;
                    uint64_t sz = 0;
                    auto fsz = fs::file_size(fit->path(), ec);
                    if (!ec) sz = static_cast<uint64_t>(fsz);
                    ++stats.attachments;
                    ++totalAttachments;
                    stats.sizeBytes += sz;
                    totalBytes += sz;
                }
            }
        }

        os << "=== Входящие: " << inboxRoot.string() << " ===\n\n";
        if (bySubject.empty())
        {
            os << "Писем не найдено.\n";
            return;
        }

        size_t idx = 0;
        for (const auto& p : bySubject)
        {
            const SubjectStats& s = p.second;
            if (s.letters == 0)
                continue;
            ++idx;
            os << "[" << idx << "] Тема: " << p.first << "\n";
            os << "    Писем: " << s.letters << ", вложений: " << s.attachments
               << ", объём: " << MailFolderScanner::formatBytes(s.sizeBytes) << "\n\n";
        }

        os << "-------------------------------------\n";
        os << "Всего писем: " << totalLetters << "\n";
        os << "Всего вложений: " << totalAttachments << "\n";
        os << "Общий объём: " << MailFolderScanner::formatBytes(totalBytes) << "\n";
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
