#include "CmdImportCsvContacts.h"

#include "core/contacts/ContactsManager.h"
#include "core/contacts/CsvContactsImporter.h"
#include "core/profile/LocalProfileCache.h"
#include "menu/Commands/Menu/CmdBack.h"
#include "menu/Commands/Menu/CmdInfo.h"
#include "menu/Commands/Menu/CmdOpenMenu.h"
#include "menu/Menu.h"
#include "Utils/Utils.h"
#include "UtilsFS/FsCompat.h"
#include "UtilsFS/FsUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace astra_mail
{
    static std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    class CmdImportCsvContactsFile final : public ICommand
    {
    public:
        explicit CmdImportCsvContactsFile(fs::path p)
                : csvPath_(std::move(p)) {}

        std::string title() const override
        {
            return "CSV: " + csvPath_.filename().string();
        }

        CommandResult execute() override
        {
            CsvContactsImporter importer;
            if (!importer.loadFromFile(csvPath_))
            {
                std::cout << "[CSV-IMPORT][ERROR] Не удалось импортировать файл: "
                          << csvPath_.string() << "\n";
                my::pause();
                return CommandResult::Continue;
            }

            std::unordered_map<std::string, Contact> existingContacts;
            std::unordered_map<std::string, std::vector<std::string>> existingLists;
            loadLocalContacts(existingContacts, existingLists);

            // merge: добавляем/обновляем контакты, списки рассылки не трогаем
            for (const auto& kv : importer.contacts())
                existingContacts[kv.first] = kv.second;

            if (!saveLocalContacts(existingContacts, existingLists))
            {
                std::cout << "[CSV-IMPORT][ERROR] Не удалось сохранить contacts.conf\n";
                my::pause();
                return CommandResult::Continue;
            }

            ContactsManager::instance().loadFromCache(
                    "csv_import",
                    existingContacts,
                    existingLists
            );

            std::cout << "[CSV-IMPORT] ОК: " << csvPath_.filename().string() << "\n"
                      << "  Загружено контактов: " << importer.contacts().size() << "\n"
                      << "  Итог контактов: " << existingContacts.size() << "\n"
                      << "  Списки рассылки: " << existingLists.size() << "\n";

            my::pause();
            return CommandResult::Continue;
        }

    private:
        fs::path csvPath_;
    };

    static fs::path guessHomeDir()
    {
        const char* home = std::getenv("HOME");
        if (home && *home)
            return fs::path(home);

#if defined(_WIN32)
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile && *userProfile)
            return fs::path(userProfile);
#endif

        return fs::current_path();
    }

    static fs::path guessDesktopDir()
    {
        const fs::path home = guessHomeDir();
        return home / "Desktop";
    }

    static fs::path guessExeDir()
    {
        return fs::path(fsu::getExecutableDir());
    }

    static std::vector<fs::path> listCsvFiles(const fs::path& folder)
    {
        std::vector<fs::path> out;
        std::error_code ec;
        if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec))
            return out;

        for (const auto& entry : fs::directory_iterator(folder, ec))
        {
            if (ec)
                break;

            const fs::path p = entry.path();
            if (!fs::is_regular_file(p, ec) || ec)
                continue;

            const std::string ext = toLower(p.extension().string());
            if (ext == ".csv")
                out.push_back(p);
        }

        std::sort(out.begin(), out.end(),
                  [](const fs::path& a, const fs::path& b)
                  {
                      return a.filename().string() < b.filename().string();
                  });
        return out;
    }

    std::string CmdImportCsvContacts::title() const
    {
        return "Импорт контактов из CSV (выбор папки/файла)";
    }

    CommandResult CmdImportCsvContacts::execute()
    {
        const fs::path desktopDir = guessDesktopDir();
        const fs::path homeDir = guessHomeDir();
        const fs::path exeDir = guessExeDir();

        auto makeFilesMenu = [&](const fs::path& folder) -> std::unique_ptr<Menu>
        {
            auto menu = std::make_unique<Menu>("CSV-файлы: " + folder.string());

            const auto files = listCsvFiles(folder);
            if (files.empty())
            {
                menu->add(std::make_unique<CmdInfo>(
                        "CSV не найдено",
                        "В папке нет файлов с расширением .csv"));
                menu->add(std::make_unique<CmdBack>());
                return menu;
            }

            for (const auto& f : files)
            {
                menu->add(std::make_unique<CmdImportCsvContactsFile>(f));
            }

            menu->add(std::make_unique<CmdBack>());
            return menu;
        };

        auto menu = std::make_unique<Menu>("Импорт CSV контактов: выбор папки");
        menu->add(std::make_unique<CmdOpenMenu>(
                [=]() { return makeFilesMenu(desktopDir); },
                "Рабочий стол"));
        menu->add(std::make_unique<CmdOpenMenu>(
                [=]() { return makeFilesMenu(homeDir); },
                "Домашняя"));
        menu->add(std::make_unique<CmdOpenMenu>(
                [=]() { return makeFilesMenu(exeDir); },
                "Рядом с exe"));

        menu->add(std::make_unique<CmdBack>());
        menu->run();

        return CommandResult::Continue;
    }
}
