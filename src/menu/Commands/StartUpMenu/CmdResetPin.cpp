#include "CmdResetPin.h"

#include "SecretManager.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/Utils.h"
#include "Utils/SensitiveString.h"
#include "Utils/SecretInput.h"
#include "core/mail/AccountAccess.h"

#include <iostream>
#include <string>

namespace astra_mail
{

    CommandResult CmdResetPin::execute()
    {
        AccountChangeScope change;
        if (!change.allowed()) { std::cout << "Дождитесь завершения почтовой операции.\n"; return CommandResult::Continue; }
        auto& tb = ThunderbirdSettings::instance();
        auto& sm = SecretManager::instance();
        const auto context = tb.secretContext();
        if (context.empty()) { std::cout << "Сначала выберите аккаунт.\n"; return CommandResult::Continue; }
        tb.clearMailPassword();

        std::string pass1, pass2;
        std::string pin1, pin2;
        ClearSensitiveOnExit wipePass1{pass1}, wipePass2{pass2}, wipePin1{pin1}, wipePin2{pin2};

        if (!readSecret("Введите новый пароль почты: ", pass1))
            return CommandResult::Continue;

        if (!readSecret("Повторите пароль: ", pass2) || pass1 != pass2)
        {
            std::cout << "Пароли не совпадают\n";
            my::pause();
            return CommandResult::Continue;
        }

        if (!readSecret("Введите новый PIN (без пробелов): ", pin1, 1024))
            return CommandResult::Continue;
        if (!validNewPin(pin1)) {
            std::cout << "Новый PIN не должен содержать пробелы или управляющие символы.\n";
            my::pause();
            return CommandResult::Continue;
        }
        if (!readSecret("Повторите PIN: ", pin2, 1024))
            return CommandResult::Continue;

        if (pin1 != pin2)
        {
            std::cout << "PIN не совпадает\n";
            my::pause();
            return CommandResult::Continue;
        }

        // перезапись секретов через твой интерфейс
        if (!sm.store(context, pass1, pin1))
        {
            std::cout << "Не удалось сохранить секреты\n";
            my::pause();
            return CommandResult::Continue;
        }

        if (!tb.setMailPassword(pass1)) return CommandResult::Continue;

        std::cout << "PIN и пароль обновлены.\n";
        my::pause();

        return CommandResult::ExitMenu;
    }

} // namespace astra_mail
