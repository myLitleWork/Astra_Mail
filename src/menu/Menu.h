#pragma once
#include <memory>
#include <vector>
#include <iostream>

#if __has_include(<optional>)
#include <optional>
  using std::optional;
#else
#include <experimental/optional>
using std::experimental::optional;
#endif

#include "Command.h"

namespace astra_mail
{

    enum class FlashContext
    {
        ViewOnly,      // просто посмотреть
        SendMails,      // выбрать флешку → отправить
        ReciveMails
    };


    class Menu
    {

    public:
        void add(std::unique_ptr<ICommand> cmd);
        void run();
        void clear();
        explicit Menu(std::string title, bool isRoot = false);

    private:
        std::vector<std::unique_ptr<ICommand>> cmds_;
        optional<int> readChoice();   // вместо int
        std::string lastInput_;

        /** Пустой или только пробелы — выход из фонового экрана. */
        bool wasBlankInput() const {
            return lastInput_.find_first_not_of(" \t\n\r") == std::string::npos;
        }

        bool runMainMenu();
        virtual void printHeader();
        virtual void printItems();

        bool isRoot_ = false;
        std::string title_;

        static void showError(const std::string& msg);

    };
}
