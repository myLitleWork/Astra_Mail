#pragma once
#include <string>
#include "menu/Command.h"

namespace astra_mail
{
    class CmdEnableUsbMount : public ICommand
    {
    public:
        std::string title() const override;
        CommandResult execute() override;

    private:
        static bool isRoot();
        static bool dirExists(const std::string& p);
        static bool fileExists(const std::string& p);

        static bool writeFileAsRoot(const std::string& path, const std::string& content, std::string& err);
        static bool runShell(const std::string& cmd, std::string& err);

        static std::string rulesPath();
        static std::string pklaPath();
        static std::string rulesContent();
        static std::string pklaContent();

        static bool askYesNo(const std::string& prompt);
        static void pause();
    };
}
