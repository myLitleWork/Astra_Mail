#pragma once
#include <string>

namespace astra_mail::imap {

    // IMAP modified UTF-7 → UTF-8
    std::string decodeUtf7(const std::string& s);

}
