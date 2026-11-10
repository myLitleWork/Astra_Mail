#pragma once
#include "core/mail/AccountAccess.h"
#include "Utils/Utils.h"
#include <iostream>

namespace astra_mail {
inline bool mailCommandAllowed(bool acquired) {
    if (acquired) return true;
    std::cout << mailBusyMessage() << "\n";
    // The menu clears the screen on return. Keep the rejection visible until
    // Enter; no mail gate is held while acknowledging a rejected command.
    my::pause();
    return false;
}
}
