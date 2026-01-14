#pragma once
#include <string>
#include "MailExportOptions.h"

class MailFolderExporter
{
public:
    MailFolderExporter(std::string baseOutDir,
                       MailSaveFlags flags);

    bool exportFromRaw(const std::string& rawEml,
                       const std::string& mailId,
                       std::string& err);

private:
    std::string baseOutDir_;
    MailSaveFlags flags_;
};
