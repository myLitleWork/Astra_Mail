//
// Created by Sg on 14.12.2025.
//

#include "MenuCreator.h"
#include "src/Utils/FlashDriveScanner.h"
#include "src/menu/Commands/Mails/CmdViewFlashMails.h"
#include "src/menu/Commands/Mails/CmdSendMailsFromPath.h"
#include "src/menu/Commands/Menu/CmdBack.h"
#include "src/menu/Commands/Flash+Mount/CmdMountFlash.h"
#include "src/menu/Commands/Menu/CmdOpenMenu.h"
#include "ThunderbirdSettings.h"
#include "src/menu/Commands/Menu/CmdInfo.h"
#include "src/menu/Commands/Flash+Mount/CmdUmountFlash.h"
#include "src/menu/Commands/Flash+Mount/CmdCleanupGarbageMounts.h"
#include "src/menu/Commands/Flash+Mount/CmdEnableUsbMount.h"
#include "menu/Commands/Server/CmdScanThunderbirdProfiles.h"
#include "Utils/Utils.h"
#include "menu/Commands/Server/CmdApplyThunderbirdAccount.h"
#include "src/menu/Commands/Mails/CmdExportUnreadToFolders.h"
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
            const std::vector<ImapFolder>& imapFolders,
            const std::string& mountPoint,
            const std::string& password
    )
    {
        auto menu = std::make_unique<Menu>(title);

        // ВСЕ ПАПКИ
        menu->add(std::make_unique<CmdExportUnreadToFolders>(
                imap.host,
                imap.port,
                imap.user,
                password,
                imapFolders,
                mountPoint,
                ExportStrategy{ mode, dedupByHash }
        ));

        // ПО ПАПКАМ ОТДЕЛЬНО
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
        auto menu = std::make_unique<Menu>("Основное меню Astra_Mail_v3.1", true);

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



       /* menu->add(
                std::make_unique<CmdExportUnreadMails>()
        );*/

        menu->add(std::make_unique<CmdBack>());

        return menu;
    }

// ==================== МЕНЮ «ФЛЕШКИ» ====================

    std::unique_ptr<Menu> createFlashMenu(FlashContext ctx)
    {
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


                    // ── получаем список папок ─────────────────────────────────────
                    std::vector<std::string> folders;
                    std::string err;

                    if (!ImapClient::listMailboxes(
                            imap.host,
                            imap.port,
                            imap.user,
                            ts.mailPassword(),
                            folders,
                            err))
                    {

                        break;
                    }

                    std::vector<ImapFolder> imapFolders;

                    for (const std::string& raw : folders)
                    {

                        std::string display = astra_mail::imap::decodeUtf7(raw);

                        if (display.empty())
                            continue;

                        imapFolders.emplace_back(raw, display);
                    }


                    astra_mail::imap::FolderFilter filter(
                            config::imapFoldersConfig()
                    );

                    filter.load();

                    for (auto it = imapFolders.begin(); it != imapFolders.end(); )
                    {
                        if (!filter.shouldShow(it->displayName))
                            it = imapFolders.erase(it);
                        else
                            ++it;
                    }


                    // ── ВСЕ ПАПКИ: по темам ────────────────────────────────────────
                    menu->add(std::make_unique<CmdOpenMenu>(
                            [=]() {
                                return buildExportMenu(
                                        "Экспорт по темам",
                                        ExportMode::BySubject,
                                        false,
                                        imap,
                                        imapFolders,
                                        d.mountPoint,
                                        ts.mailPassword()
                                );
                            },
                            "Экспорт по темам"
                    ));


                    menu->add(std::make_unique<CmdOpenMenu>(
                            [=]() {
                                return buildExportMenu(
                                        "Экспорт по типам вложений",
                                        ExportMode::ByExtension,
                                        true,
                                        imap,
                                        imapFolders,
                                        d.mountPoint,
                                        ts.mailPassword()
                                );
                            },
                            "Экспорт по типам вложений"
                    ));


                    if(d.mounted)
                    {
                        menu->add(std::make_unique<CmdUmountFlash>(d));
                    }

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

        menu->add(std::make_unique<CmdInfo>(
                "Справка: правила темы письма",
                config::defaults::subjectRulesHelp()
        ));

        menu->add(std::make_unique<CmdInfo>(
                "Справка: группы вложений",
                config::defaults::attachmentGroupsHelp()
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
            menu->add(std::make_unique<CmdInfo>("Ошибка загрузки prefs.js", config::defaults::tbPrefsLoadError()));
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




} // namespace astra_mail
