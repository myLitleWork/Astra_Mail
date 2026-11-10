#pragma once

#include <iostream>
#include <string>

namespace astra_mail {
// A command is submitted only by a complete line. Keep EOF/error flags so
// callers and parent menus can unwind without retrying a closed input stream.
inline bool readConsoleLine(std::string& value) {
    if (!std::getline(std::cin, value) || !std::cin.good()) {
        value.clear();
        return false;
    }
    return true;
}
}
