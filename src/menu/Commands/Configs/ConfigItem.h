// ConfigItem.h
#pragma once
#include <string>
#include "UtilsFS/FsCompat.h"

namespace astra_mail
{
    struct ConfigItem
    {
        std::string title;
        fs::path path;
    };
}
