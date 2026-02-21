//
// Created by Sg on 18.12.2025.
//
#pragma once

#include <vector>
#include "menu/Command.h"

namespace astra_mail {

    struct ParsedMail
    {
        std::string subject;
        std::string from;
        std::string to;
        std::string date;
        std::string bodyText;

        struct Attachment
        {
            std::string filename;
            std::vector<char> data;
        };

        std::vector<Attachment> attachments;
    };

    class CmdParseEmlFolder : public ICommand
    {
    public:
        CmdParseEmlFolder(std::string inDir,
                          std::string outDir);

        std::string title() const override;
        CommandResult execute() override;

    private:
        std::string inDir_;
        std::string outDir_;
    };


} // astra_mail
