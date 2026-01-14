#pragma once
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"

namespace astra_mail {


    bool saveActiveProfile(const TbProfile& profile);
    bool loadActiveProfile(TbProfile& out);



} // astra_mail
