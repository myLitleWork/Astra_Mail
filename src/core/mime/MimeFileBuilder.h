#pragma once
#include <string>
#include "Utils/EmailMessage.h"

class MimeFileBuilder
{
public:
    static bool buildToFile(const EmailMessage& msg,
                            const std::string& outPath,
                            std::string& err);
};
