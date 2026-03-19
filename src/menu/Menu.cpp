#include "Menu.h"
#include "core/idle/IdleBackground.h"
#include <thread>
#include <chrono>
#include <utility>

namespace astra_mail
{





    static void clearScreen()
    {
        // ANSI escape: очистка экрана + курсор в (1,1)
        std::cout << "\033[2J\033[H";
        std::cout.flush();
    }

    void Menu::showError(const std::string& msg)
    {
        clearScreen();
        std::cout << msg << "\n";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void Menu::add(std::unique_ptr<ICommand> cmd)
    {
        cmds_.push_back(std::move(cmd));
    }

    void Menu::printHeader()
    {
        std::cout << "====== "<< title_ << " ======\n";
    }


    void Menu::run()
    {
        while (true)
        {
            clearScreen();

            bool stay = runMainMenu();
            if (!stay)
            {
                if (isRoot_)
                    return;        // корневое меню → выход из приложения
                else
                    return;        // подменю → возврат к родителю
            }
        }
    }



    bool Menu::runMainMenu()
    {
        clearScreen();
        printHeader();
        printItems();

        auto cOpt = readChoice();
        if (!cOpt)
        {
            if (wasBlankInput())
                return true;  // Enter без ввода — выход из фонового экрана, обновить меню
            showError("Неверный выбор");
            return true;
        }

        int c = *cOpt;
        if (c == 0)
            return false;

        if (c < 1 || c > (int)cmds_.size())
        {
            showError("Неверный выбор");
            return true;
        }

        IdleBackground::instance().resetTimer();

        auto res = cmds_[c - 1]->execute();

        if (res == CommandResult::ExitMenu)
            return false;   // выйти из текущего меню


        return true;
    }



    void Menu::printItems()
    {
        for (size_t i = 0; i < cmds_.size(); ++i)
            std::cout << (i + 1) << ". " << cmds_[i]->title() << "\n";

      if(isRoot_)
        std::cout << "0. Выход\n";
    }



    optional<int> Menu::readChoice()
    {
        std::cout << "Выбор: ";
        std::cout.flush();

        std::string s;
        if (!std::getline(std::cin, s))
            return std::experimental::nullopt;

        lastInput_ = s;

        // Пустой ввод (только Enter) — выход из фонового экрана
        if (s.find_first_not_of(" \t\n\r") == std::string::npos)
            return std::experimental::nullopt;

        try
        {
            size_t pos = 0;
            int v = std::stoi(s, &pos);

            // запрет "12abc"
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
            if (pos != s.size()) return std::experimental::nullopt;

            return v;
        }
        catch (...)
        {
            return std::experimental::nullopt;
        }
    }

    Menu::Menu(std::string title, bool isRoot)
    : title_{std::move(title)}, isRoot_{isRoot}
    {

    }

    void Menu::clear() {
        cmds_.clear();
    }


}
