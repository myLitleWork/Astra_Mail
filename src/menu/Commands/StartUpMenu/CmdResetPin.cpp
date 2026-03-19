#include "CmdResetPin.h"

#include "SecretManager.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/Utils.h"

#include <iostream>
#include <string>

namespace astra_mail
{

    static bool readHiddenOrPlain(const char* prompt, std::string& out)
    {
        std::cout << prompt;

        // если у тебя есть скрытый ввод — используй его
        // иначе временно обычный ввод
        // return my::readHidden(out);

        std::getline(std::cin >> std::ws, out);
        return !out.empty();
    }

    CommandResult CmdResetPin::execute()
    {
        auto& tb = ThunderbirdSettings::instance();
        auto& sm = SecretManager::instance();

        std::string pass1, pass2;
        std::string pin1, pin2;

        if (!readHiddenOrPlain("Введите новый пароль почты: ", pass1))
            return CommandResult::Continue;

        if (!readHiddenOrPlain("Повторите пароль: ", pass2) || pass1 != pass2)
        {
            std::cout << "Пароли не совпадают\n";
            my::pause();
            return CommandResult::Continue;
        }

        std::cout << "Введите новый PIN: ";
        std::getline(std::cin >> std::ws, pin1);

        std::cout << "Повторите PIN: ";
        std::getline(std::cin >> std::ws, pin2);

        if (pin1 != pin2)
        {
            std::cout << "PIN не совпадает\n";
            my::pause();
            return CommandResult::Continue;
        }

        // перезапись секретов через твой интерфейс
        if (!sm.store(pass1, pin1))
        {
            std::cout << "Не удалось сохранить секреты\n";
            my::pause();
            return CommandResult::Continue;
        }

        tb.setMailPassword(pass1);

        std::cout << "PIN и пароль обновлены.\n";
        my::pause();

        return CommandResult::ExitMenu;
    }

} // namespace astra_mail
