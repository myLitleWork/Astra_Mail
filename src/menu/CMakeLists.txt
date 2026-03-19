set(MENU_CPP
        ${CMAKE_SOURCE_DIR}/src/menu/AttachmentExporter.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Menu.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/MenuCreator.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/MenuSeparator.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/StartupProfileLoader.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/StartupServerCheck.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Configs/CmdEditConfig.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Configs/CmdResetConfig.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Flash+Mount/CmdCleanupGarbageMounts.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Flash+Mount/CmdEnableUsbMount.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Flash+Mount/CmdMountFlash.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Flash+Mount/CmdUmountFlash.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Menu/CmdBack.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Menu/CmdInfo.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Menu/CmdOpenMenu.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdCleanupInbox.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdCleanupOutbox.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdExportLocalInboxToFlash.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdExportUnreadToFolders.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdExportUnreadToInbox.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdParseEmlFolder.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdSendMailsFromOutbox.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdSendMailsFromPath.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdViewFlashMails.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdViewInboxMails.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Mails/CmdViewOutboxMails.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdApplyThunderbirdAccount.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdCheckServers.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdScanThunderbirdProfiles.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdShowImapSettings.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdShowLocalContacts.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdShowLocalProfileSettings.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdImportCsvContacts.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/Server/CmdShowSmtpSettings.cpp

        ${CMAKE_SOURCE_DIR}/src/menu/Commands/StartUpMenu/CmdEnterPin.cpp
        ${CMAKE_SOURCE_DIR}/src/menu/Commands/StartUpMenu/CmdResetPin.cpp
        )

add_library(AstraMail_menu STATIC
        ${MENU_CPP}
        )

target_include_directories(AstraMail_menu PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/secrets
        )

# Menu code calls into Utils/Resolver/Configs/Secrets implementations.
target_link_libraries(AstraMail_menu PRIVATE
        AstraMail_utils
        AstraMail_utilsfs
        AstraMail_resolver
        AstraMail_configs
        AstraMail_secrets
        )

