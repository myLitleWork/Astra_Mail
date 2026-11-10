#pragma once
#include <string>
#include <cstdio>
#include "Utils/EmailMessage.h"

class MimeFileBuilder
{
public:
    // The caller owns the already opened private stream. No path is reopened.
    static bool buildToStream(const EmailMessage& msg,
                            FILE* file,
                            std::string& err);
};
