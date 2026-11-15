//
// Created by Sg on 14.12.2025.
//

#pragma once
#include "menu/Menu.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"

namespace astra_mail::menu
{
        std::unique_ptr<Menu> createFlashMenu(FlashContext ctx);

        std::unique_ptr<Menu> createOutboxMenu();
        std::unique_ptr<Menu> createInboxMenu();

        std::unique_ptr<Menu> createMainFlashMenu();

        std::unique_ptr<Menu> createThunderbirdAccountsMenu(const TbProfile& profile);

        std::unique_ptr<Menu> createMainMenu();

        std::unique_ptr<Menu> createSystemMenu();

        std::unique_ptr<Menu> createSettingsMenu();

        std::unique_ptr<Menu> createAccountSettingsMenu();
        std::unique_ptr<Menu> createContactsSettingsMenu();
        std::unique_ptr<Menu> createMailRulesSettingsMenu();
        std::unique_ptr<Menu> createResetSettingsMenu();
        std::unique_ptr<Menu> createTutorialsMenu();
        std::unique_ptr<Menu> createUsbMenu();
        std::unique_ptr<Menu> createUsbDrivesMenu();

        std::unique_ptr<Menu> createThunderbirdProfilesMenu();


}


