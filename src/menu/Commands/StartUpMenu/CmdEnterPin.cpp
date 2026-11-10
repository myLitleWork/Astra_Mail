#include "Utils/ConsoleInput.h"
#include "CmdEnterPin.h"

#include "SecretManager.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/Utils.h"
#include "Utils/SensitiveString.h"
#include "Utils/SecretInput.h"
#include "core/mail/AccountAccess.h"

#include <iostream>

namespace astra_mail
{

    CommandResult CmdEnterPin::execute()
    {
        AccountChangeScope change;
        if (!change.allowed()) { std::cout << "Дождитесь завершения почтовой операции.\n"; return CommandResult::Continue; }
        auto& sm = SecretManager::instance();
        auto& tb = ThunderbirdSettings::instance();
        const auto context = tb.secretContext();
        if (context.empty()) { std::cout << "Сначала выберите аккаунт.\n"; return CommandResult::Continue; }
        tb.clearMailPassword();

        // 1) если секретов нет (нет файла/не инициализировано) — выходим из меню
        if (!sm.isInitialized())
        {
            std::cout << "Секреты не инициализированы (нет сохранённого пароля/файла).\n";
            my::pause();
            return CommandResult::ExitMenu;
        }

        // 2) ввод PIN
        std::string pin;
        ClearSensitiveOnExit wipePin{pin};
        if (!readSecret("Введите PIN: ", pin, 1024)) return CommandResult::Continue;

        // 3) расшифрование
        std::string password;
        ClearSensitiveOnExit wipePassword{password};
        if (!sm.hasSecret(context) && sm.legacyAvailable(context))
        {
            const auto& active = tb.active();
            const auto& imap = tb.imapServers().at(active.imapServerId);
            const auto& smtp = tb.smtpServers().at(active.smtpServerId);
            std::cout << "Старое хранилище не привязано к аккаунту.\n"
                      << "Профиль: " << tb.currentProfile().id << ", аккаунт: " << active.accountId << "\n"
                      << "IMAP: " << imap.user << " @ " << imap.host << ":" << imap.port << "\n"
                      << "SMTP: " << smtp.user << " @ " << smtp.host << ":" << smtp.port << "\n"
                      << "Подтвердите, что старый пароль принадлежит этому аккаунту.\n"
                      << "Исходный secret.bin будет сохранён. Для привязки введите yes: ";
            std::string answer;
            if (!readConsoleLine(answer)) return CommandResult::Continue;
            if (answer != "yes") return CommandResult::Continue;
            if (!sm.migrateLegacy(context, pin)) {
                std::cout << "Не удалось перенести старый пароль. Аккаунт остаётся заблокирован.\n";
                return CommandResult::Continue;
            }
        }
        if (!sm.load(context, pin, password))
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
        if (!tb.setMailPassword(password)) return CommandResult::Continue;

        std::cout << "PIN принят. Пароль загружен.\n";
        my::pause();
        return CommandResult::ExitMenu;
    }

} // namespace astra_mail
