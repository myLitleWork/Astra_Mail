#pragma once

#include "UtilsFS/FsCompat.h"

namespace astra_mail
{
    /** Корень папки outbox: ~/.local/share/AstraMail/outbox */
    fs::path getOutboxRoot();

    /** Корень папки inbox: ~/.local/share/AstraMail/inbox */
    fs::path getInboxRoot();
}
