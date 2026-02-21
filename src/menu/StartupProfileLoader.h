#pragma once


#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "menu/Menu.h"
#include "src/menu/Commands/StartUpMenu/CmdEnterPin.h"
#include "src/menu/Commands/StartUpMenu/CmdResetPin.h"
#include "SecretManager.h"
#include "ThunderbirdSettings.h"

namespace astra_mail
{

    bool pinMenu();

    bool profileSetupMenu();

    bool tryLoadSavedProfile(TbProfile& outProfile);

    /** Проверка подключения к IMAP и SMTP серверам активного аккаунта. Возвращает true только если оба доступны. */
    bool checkServers();

    /** То же без вывода и паузы (для фонового потока). */
    bool checkServersSilent();
}
