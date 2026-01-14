#include "CmdEnterPin.h"

#include "SecretManager.h"
#include "ThunderbirdSettings.h"
#include "Utils/Utils.h"

#include <iostream>

namespace astra_mail
{

    CommandResult CmdEnterPin::execute()
    {
        auto& sm = SecretManager::instance();

        // 1) если секретов нет (нет файла/не инициализировано) — выходим из меню
        if (!sm.isInitialized())
        {
            std::cout << "Секреты не инициализированы (нет сохранённого пароля/файла).\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        // 2) ввод PIN
        std::string pin;
        std::cout << "Введите PIN: ";
        std::cin >> pin;

        // 3) расшифрование
        std::string password;
        if (!sm.load(pin, password))
        {
            std::cout << "Неверный PIN или не удалось расшифровать пароль.\n";
            return CommandResult::Continue; // остаёмся в PIN-меню
        }

        // 4) проверка успешности (минимальная): пароль не пустой
        if (password.empty())
        {
            std::cout << "Пароль расшифрован, но пустой. Проверьте хранилище.\n";
            return CommandResult::Continue;
        }

        // 5) кладём пароль в настройки
        ThunderbirdSettings::instance().setMailPassword(password);

        std::cout << "PIN принят. Пароль загружен.\n";
        my::pause();
        return CommandResult::ExitMenu;
    }

} // namespace astra_mail
