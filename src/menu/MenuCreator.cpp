//
// Created by Sg on 14.12.2025.
//

#include "MenuCreator.h"
#include "AstraMailVersion.h"
#include "Utils/FlashDriveScanner.h"
#include "menu/Commands/Mails/CmdViewFlashMails.h"
#include "menu/Commands/Mails/CmdSendMailsFromPath.h"
#include "menu/Commands/Mails/CmdViewOutboxMails.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "menu/Commands/Mails/CmdViewInboxMails.h"
#include "menu/Commands/Mails/CmdExportUnreadToInbox.h"
#include "menu/Commands/Mails/CmdCleanupOutbox.h"
#include "menu/Commands/Mails/CmdCleanupInbox.h"
#include "menu/Commands/Menu/CmdBack.h"
#include "menu/Commands/Flash+Mount/CmdMountFlash.h"
#include "menu/Commands/Menu/CmdOpenMenu.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "menu/Commands/Menu/CmdInfo.h"
#include "menu/Commands/Flash+Mount/CmdUmountFlash.h"
#include "menu/Commands/Flash+Mount/CmdCleanupGarbageMounts.h"
#include "menu/Commands/Flash+Mount/CmdEnableUsbMount.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "menu/Commands/Mails/CmdReviewDelivery.h"
#include "menu/Commands/StartUpMenu/CmdUnlockAccount.h"
#include "core/mail/AccountAccess.h"
#include "Utils/Utils.h"
#include "menu/Commands/Server/CmdApplyThunderbirdAccount.h"
#include "menu/Commands/Server/CmdCheckServers.h"
#include "menu/Commands/Server/CmdShowLocalProfileSettings.h"
#include "menu/Commands/Server/CmdShowLocalContacts.h"
#include "menu/Commands/Server/CmdImportCsvContacts.h"
#include "menu/Commands/Mails/CmdExportUnreadToFolders.h"

#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"

#include "menu/Commands/Configs/CmdEditConfig.h"
#include "Configs/Confg/ConfigPaths.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "menu/Commands/Configs/CmdResetConfig.h"
#include "Configs/Confg/ConfigDefaults.h"
#include "Utils/ImapClient.h"
#include "Utils/ImapUtf7.h"
#include "Utils/ImapFolderFilter.h"

#include <iomanip>
#include <sstream>
#include <algorithm>

namespace astra_mail::menu
{


    auto normalizeFolders = [](std::vector<std::string>& folders)
    {
        // decode UTF-7
        for (auto& f : folders)
            f = astra_mail::imap::decodeUtf7(f);

        // remove empty
        folders.erase(
                std::remove_if(folders.begin(), folders.end(),
                               [](const std::string& f){ return f.empty(); }),
                folders.end()
        );

        // deduplicate
        std::sort(folders.begin(), folders.end());
        folders.erase(std::unique(folders.begin(), folders.end()),
                      folders.end());

        // filter service folders
        auto isService = [](const std::string& f)
        {
            return f == "/" ||
                   f.find("Trash") != std::string::npos ||
                   f.find("Spam")  != std::string::npos ||
                   f.find("Junk")  != std::string::npos;
        };

        folders.erase(
                std::remove_if(folders.begin(), folders.end(), isService),
                folders.end()
        );
    };


    std::unique_ptr<Menu> buildExportMenu(
            const std::string& title,
            ExportMode mode,
            bool dedupByHash,
            const ImapConfig& imap,
            const std::string& mountPoint,
            const std::string& password
    )
    {
        auto menu = std::make_unique<Menu>(title);

        std::vector<ImapFolder> imapFolders;
        {
            std::vector<std::string> rawFolders;
            std::string err;
            if (ImapClient::listMailboxes(imap.host, imap.port, imap.user, password, rawFolders, err))
            {
                for (const std::string& raw : rawFolders)
                {
                    std::string display = astra_mail::imap::decodeUtf7(raw);
                    if (!display.empty())
                        imapFolders.emplace_back(raw, display);
                }

                astra_mail::imap::FolderFilter filter(config::imapFoldersConfig());
                filter.load();
                for (auto it = imapFolders.begin(); it != imapFolders.end(); )
                {
                    if (!filter.shouldShow(it->displayName))
                        it = imapFolders.erase(it);
                    else
                        ++it;
                }
            }
        }

        menu->add(std::make_unique<CmdExportUnreadToFolders>(
                imap.host,
                imap.port,
                imap.user,
                password,
                imapFolders,
                mountPoint,
                ExportStrategy{ mode, dedupByHash }
        ));

        for (const auto& folder : imapFolders)
        {
            menu->add(std::make_unique<CmdExportUnreadToFolders>(
                    imap.host,
                    imap.port,
                    imap.user,
                    password,
                    folder,
                    mountPoint,
                    ExportStrategy{ mode, dedupByHash }
            ));
        }

        menu->add(std::make_unique<CmdBack>());
        return menu;
    }




    std::string formatThunderbirdProfileTitle(const TbProfile& p)
        {
            auto yesno = [](bool v){ return v ? "да" : "нет"; };

            std::ostringstream ss;
            ss << p.name;

            if (p.isDefaultProfile)
                ss << " [default]";

            ss << "\n"
               << "    Версия TB      : "
               << (p.tbVersion.empty() ? "?" : p.tbVersion) << "\n"
               << "    Активность     : "
               << (p.lastMailActivity
                   ? my::formatTime(p.lastMailActivity)
                   : "нет данных");

            return ss.str();
        }

    std::string profileTitle(const TbProfile& p)
    {
        std::string t;

        t += "Идентификатор профиля:\t" + p.name + "\n";

        if (p.lastMailActivity != 0)
        {
            t += "   - активность почты:\t";
            t += my::formatTime(p.lastMailActivity);
        }
        else if (p.lastModified != 0)
        {
            t += "   - последние изменения:\t";
            t += my::formatTime(p.lastModified);
        }
        else
        {
            t += "   - статус\t:\tнет данных";
        }

        return t;
    }

// ==================== ОСНОВНОЕ МЕНЮ ====================

    std::unique_ptr<Menu> createMainMenu()
    {
        auto menu = std::make_unique<Menu>(std::string("Основное меню Astra_Mail_v") + ASTRA_MAIL_APP_VERSION, true);

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createMainFlashMenu(); },
                        "Работа с письмами"
                )
        );

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createSettingsMenu(); },
                        "Настройки"
                )
        );


        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createSystemMenu(); },
                        "Система"
                )
        );


        return menu;
    }

// ==================== МЕНЮ «РАБОТА С ПИСЬМАМИ» ====================

    std::unique_ptr<Menu> createMainFlashMenu()
    {
        auto menu = std::make_unique<Menu>("Работа с письмами");


        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createFlashMenu(FlashContext::SendMails); },
                        "Отправка писем c флеш-накопителей"
                )
        );

        menu->add(
                std::make_unique<CmdOpenMenu>( []() { return createFlashMenu(FlashContext::ReciveMails); },
                                               "Сохранить непрочитанные письма на флешку" )
        );

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createFlashMenu(FlashContext::ViewOnly); },
                        "Просмотр писем на флеш-накопителях"
                )
        );

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createOutboxMenu(); },
                        "Локальные исходящие"
                )
        );

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createInboxMenu(); },
                        "Локальные входящие"
                )
        );



       /* menu->add(
                std::make_unique<CmdExportUnreadMails>()
        );*/

        menu->add(std::make_unique<CmdBack>());

        return menu;
    }

// ==================== МЕНЮ «ФЛЕШКИ» ====================

    std::unique_ptr<Menu> createFlashMenu(FlashContext ctx)
    {
        MailOperationScope operation(std::try_to_lock);
        if (!operation.allowed()) {
            auto menu = std::make_unique<Menu>(mailBusyMessage());
            menu->add(std::make_unique<CmdBack>()); return menu;
        }
        FlashDriveScanner scanner;
        scanner.scan();

        auto menu = std::make_unique<Menu>("Флешки");
        auto drivers = scanner.getDrives();

        if (drivers.empty())
        {
            menu->add(std::make_unique<CmdInfo>(
                    "Флеш-накопители не найдены",
                    config::defaults::noFlashDrivesFound()
            ));
            menu->add(std::make_unique<CmdBack>());
            return menu;
        }

        std::vector<FlashDriveInfo> umd;

        for (const auto& d : drivers)
        {

            if(!d.mounted)
            {
                umd.push_back(d);
                continue;
            }

            switch (ctx)
            {
                case FlashContext::ViewOnly:
                    menu->add(std::make_unique<CmdViewFlashMails>(d.mountPoint));
                    break;

                case FlashContext::SendMails:
                    menu->add(std::make_unique<CmdSendMailsFromPath>(d.mountPoint));
                    break;

                case FlashContext::ReciveMails:
                {
                    auto& ts = ThunderbirdSettings::instance();

                    if (!ts.active().valid())
                    {
                        menu->add(std::make_unique<CmdInfo>(
                                "Почтовый аккаунт не выбран",
                                config::defaults::tbNoActiveAccount()
                        ));
                        break;
                    }

                    if (!ts.hasMailPassword())
                    {
                        menu->add(std::make_unique<CmdInfo>(
                                "Не задан пароль почты",
                                config::defaults::tbNoMailPassword()
                        ));
                        break;
                    }

                    const auto& active = ts.active();
                    const auto& imap   = ts.imapServers().at(active.imapServerId);
                    const auto accountContext = ts.secretContext();

                    menu->add(std::make_unique<CmdOpenMenu>(
                            [=, &ts]() {
                                MailOperationScope operation(std::try_to_lock);
                                if (!operation.allowed()) {
                                    auto menu = std::make_unique<Menu>(mailBusyMessage());
                                    menu->add(std::make_unique<CmdBack>()); return menu;
                                }
                                if (ts.secretContext() != accountContext || !ts.hasMailPassword()) {
                                    auto locked = std::make_unique<Menu>("Аккаунт изменён. Откройте экспорт заново после ввода PIN.");
                                    locked->add(std::make_unique<CmdBack>()); return locked;
                                }
                                return buildExportMenu(
                                        "Экспорт по темам",
                                        ExportMode::BySubject,
                                        false,
                                        imap,
                                        d.mountPoint,
                                        ts.mailPassword()
                                );
                            },
                            "Экспорт по темам → " + d.mountPoint
                    ));

                    menu->add(std::make_unique<CmdOpenMenu>(
                            [=, &ts]() {
                                MailOperationScope operation(std::try_to_lock);
                                if (!operation.allowed()) {
                                    auto menu = std::make_unique<Menu>(mailBusyMessage());
                                    menu->add(std::make_unique<CmdBack>()); return menu;
                                }
                                if (ts.secretContext() != accountContext || !ts.hasMailPassword()) {
                                    auto locked = std::make_unique<Menu>("Аккаунт изменён. Откройте экспорт заново после ввода PIN.");
                                    locked->add(std::make_unique<CmdBack>()); return locked;
                                }
                                return buildExportMenu(
                                        "Экспорт по типам вложений",
                                        ExportMode::ByExtension,
                                        true,
                                        imap,
                                        d.mountPoint,
                                        ts.mailPassword()
                                );
                            },
                            "Экспорт по типам вложений → " + d.mountPoint
                    ));

                    if(d.mounted)
                        menu->add(std::make_unique<CmdUmountFlash>(d));

                    break;
                }

            }

        }

        if(umd.size() == drivers.size())
        {
            auto menu_2 = std::make_unique<Menu>("Примонтированных флешек не найдено! Примонтируйте флешку чтобы продолжить");

            for (const auto& d : drivers)
                menu_2->add(std::make_unique<CmdMountFlash>(d));

            menu_2->add(std::make_unique<CmdBack>());
            return menu_2;
        }

        menu->add(std::make_unique<CmdBack>());
        return menu;
    }




    // ==================== МЕНЮ «СИСТЕМА» ====================
    std::unique_ptr<Menu> createSystemMenu()
    {

        auto menu = std::make_unique<Menu>("Система");

        FlashDriveScanner scanner;
        scanner.scan();

        auto drivers = scanner.getDrives();
        for (const auto& d : drivers)
        {
            if (d.mounted)
                menu->add(std::make_unique<CmdUmountFlash>(d));
            else
                menu->add(std::make_unique<CmdMountFlash>(d));
        }

        menu->add(
                std::make_unique<CmdOpenMenu>(
                        []() { return createThunderbirdProfilesMenu(); },
                        "Профили Thunderbird"
                )
        );

        menu->add(std::make_unique<CmdShowLocalProfileSettings>());
        menu->add(std::make_unique<CmdUnlockAccount>());
        menu->add(std::make_unique<CmdShowLocalContacts>());

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{ "Редактировать настройки профиля", config::localMailSettings() }));

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{ "Редактировать абонентов", config::localContacts() }));

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать настройки профиля (по умолчанию)",
                [](bool force) { config::ensureLocalMailSettings(force); }));

        menu->add(std::make_unique<CmdImportCsvContacts>());

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать адресную книгу (по умолчанию)",
                [](bool force) { config::ensureLocalContacts(force); }));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: настройки профиля",
                config::defaults::localMailSettingsHelp()));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: адресная книга",
                config::defaults::localContactsHelp()));

        menu->add(std::make_unique<CmdCheckServers>());
        menu->add(std::make_unique<CmdEnableUsbMount>());

        menu->add(
                std::make_unique<CmdCleanupGarbageMounts>()
        );

        menu->add(std::make_unique<CmdBack>());

        return menu;
    }

    std::unique_ptr<Menu> createThunderbirdProfilesMenu()
    {
        auto menu = std::make_unique<Menu>("Выбор профиля");

        auto profiles = CmdScanThunderbirdProfiles::getProfiles();

        if (profiles.empty())
        {
            menu->add(std::make_unique<CmdInfo>("Профили Thunderbird не найдены", config::defaults::tbPrefsLoadError()));
            menu->add(std::make_unique<CmdBack>());
            return menu;
        }

        for (const auto& p : profiles)
        {
            menu->add(
                    std::make_unique<CmdOpenMenu>(
                            [p]() { return createThunderbirdAccountsMenu(p); },
                            formatThunderbirdProfileTitle(p)
                    )
            );
        }

        menu->add(std::make_unique<CmdBack>());
        return menu;
    }

    std::unique_ptr<Menu> createSettingsMenu()
    {
        auto menu = std::make_unique<Menu>("Настройки");

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{
                        "Редактировать правила темы письма",
                        config::subjectRules()
                }));

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать правила темы письма",
                [](bool force)
                {
                    config::ensureSubjectRules(force);
                }));

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{
                        "Редактировать группы вложений",
                        config::attachmentGroups()
                }));

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать группы вложений",
                [](bool force)
                {
                    config::ensureAttachmentGroups(force);
                }));

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{
                        "Редактировать правила темы исходящего",
                        config::outgoingSubjectRules()
                }));

        menu->add(std::make_unique<CmdEditConfig>(
                ConfigItem{
                        "Интервалы фонового режима (бездействие)",
                        config::idleSettings()
                }));

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать правила темы исходящего",
                [](bool force)
                {
                    config::ensureOutgoingSubjectRules(force);
                }));

        menu->add(std::make_unique<CmdResetConfig>(
                "Пересоздать настройки фонового режима",
                [](bool force)
                {
                    config::ensureIdleSettings(force);
                }));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: правила темы письма",
                config::defaults::subjectRulesHelp()
        ));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: группы вложений",
                config::defaults::attachmentGroupsHelp()
        ));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: правила темы исходящего",
                config::defaults::outgoingSubjectRulesHelp()
        ));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: интервалы фонового режима",
                config::defaults::idleSettingsHelp()
        ));

        menu->add(std::make_unique<CmdBack>());

        return menu;
    }

    std::unique_ptr<Menu>
    createThunderbirdAccountsMenu(const TbProfile& profile)
    {
        auto menu = std::make_unique<Menu>(
                "Аккаунты — " + profile.name
        );

        auto& tb = ThunderbirdSettings::instance();

        if (!tb.load(profile.path + "/prefs.js"))
        {
            menu->add(std::make_unique<CmdInfo>("Профиль не открыт",
                "Дождитесь завершения почтовой операции и проверьте доступность prefs.js."));
            menu->add(std::make_unique<CmdBack>());
            return menu;
        }

        const auto& accounts = tb.accounts();

        if (accounts.empty())
        {
            menu->add(std::make_unique<CmdInfo>("В профиле нет аккаунтов", config::defaults::tbNoAccounts()));
            menu->add(std::make_unique<CmdBack>());
            return menu;
        }

        for (const auto& kv : accounts)
        {
            const AccountConfig& acc = kv.second;

            menu->add(
                    std::make_unique<CmdApplyThunderbirdAccount>(acc,profile)
            );
        }

        menu->add(std::make_unique<CmdBack>());
        return menu;
    }

// ==================== МЕНЮ «ИСХОДЯЩИЕ» ====================

    std::unique_ptr<Menu> createOutboxMenu()
    {
        auto menu = std::make_unique<Menu>("Исходящие");

        menu->add(std::make_unique<CmdViewOutboxMails>());
        menu->add(std::make_unique<CmdSendMailsFromOutbox>());
        menu->add(std::make_unique<CmdReviewDelivery>());
        menu->add(std::make_unique<CmdCleanupOutbox>());
        menu->add(std::make_unique<CmdBack>());

        return menu;
    }

// ==================== МЕНЮ «ВХОДЯЩИЕ» ====================

    std::unique_ptr<Menu> createInboxMenu()
    {
        auto menu = std::make_unique<Menu>("Входящие");

        menu->add(std::make_unique<CmdViewInboxMails>());
        menu->add(std::make_unique<CmdExportUnreadToInbox>());
        menu->add(std::make_unique<CmdOpenMenu>(
                []() { return createFlashMenu(FlashContext::ReciveMails); },
                "Экспорт на флешку"
        ));
        menu->add(std::make_unique<CmdCleanupInbox>());
        menu->add(std::make_unique<CmdBack>());

        return menu;
    }




} // namespace astra_mail
