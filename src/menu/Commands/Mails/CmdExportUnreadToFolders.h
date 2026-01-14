#pragma once
#include "menu/Command.h"
#include "menu/AttachmentExporter.h"
#include <string>
#include <utility>

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



        [[nodiscard]] std::string title() const override;
        CommandResult execute() override;

    private:

        std::string host_;
        int port_;
        std::string user_;
        std::string pass_;
        std::string outDir_;

        std::vector<ImapFolder> folders_;

        ExportStrategy strategy_;

    };

}
