#pragma once
#include <string>
#include "src/Utils/EmailMessage.h"

class MimeFileBuilder
{
public:
    static bool buildToFile(const EmailMessage& msg,
                            const std::string& outPath,
                            std::string& err);
};
