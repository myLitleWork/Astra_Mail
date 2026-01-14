#pragma once
#include "UtilsFS/FsCompat.h"

#include <string>

namespace astra_mail::system
{
    bool openInEditor(const fs::path& file,
                      std::string& err);
}
