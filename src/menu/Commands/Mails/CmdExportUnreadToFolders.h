#pragma once
#include "menu/Command.h"
#include "menu/AttachmentExporter.h"
#include <string>
#include <utility>
#include <ostream>

namespace astra_mail {


    struct ImapFolder
    {
        std::string imapName;
        std::string displayName;
        explicit ImapFolder(std::string iname, std::string dname)
        : imapName(std::move(iname)), displayName(std::move(dname)){}

    };

    class CmdExportUnreadToFolders : public ICommand
    {
    public:

        CmdExportUnreadToFolders(std::string host,
                                 int port,
                                 std::string user,
                                 std::string pass,
                                 ImapFolder folder,
                                 std::string outDir,
                                 ExportStrategy strategy);

        CmdExportUnreadToFolders(std::string host,
                                 int port,
                                 std::string user,
                                 std::string pass,
                                 const std::vector<ImapFolder>& folders,
                                 std::string outDir,
                                 ExportStrategy strategy);



        std::string title() const override;
        CommandResult execute() override;

        /** Ядро экспорта непрочитанных в каталог. log == nullptr — тихий режим (для фона). */
        static bool runExport(const std::string& host, int port,
                             const std::string& user, const std::string& pass,
                             const std::vector<ImapFolder>& folders,
                             const std::string& outDir,
                             ExportStrategy strategy,
                             std::ostream* log);

        /** Офлайн-режим: переносит письма из локального inbox в outDir (обычно на флешку), без IMAP. */
        static bool runExportFromLocalInbox(const std::string& outDir,
                                            ExportStrategy strategy,
                                            std::ostream* log);

    private:

        std::string host_;
        int port_;
        std::string user_;
        std::string accountContext_;
        std::string outDir_;

        std::vector<ImapFolder> folders_;

        ExportStrategy strategy_;

    };

}
