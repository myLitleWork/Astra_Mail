#include "CmdEnableUsbMount.h"
#include "UtilsFS/FsCompat.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>

#if __has_include(<optional>)
#include <optional>
  using std::optional;
#else
#include <experimental/optional>
using std::experimental::optional;
#endif






namespace astra_mail
{
    std::string CmdEnableUsbMount::title() const
    {
        return "Разрешить монтирование USB без пароля";
    }

    bool CmdEnableUsbMount::isRoot()
    {
        return ::geteuid() == 0;
    }

    bool CmdEnableUsbMount::dirExists(const std::string& p)
    {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_directory(p, ec);
    }

    bool CmdEnableUsbMount::fileExists(const std::string& p)
    {
        std::error_code ec;
        return fs::exists(p, ec) && fs::is_regular_file(p, ec);
    }

    void CmdEnableUsbMount::pause()
    {
        std::cout << "\nНажмите Enter...";
        std::string s;
        std::getline(std::cin, s);
    }

    bool CmdEnableUsbMount::askYesNo(const std::string& prompt)
    {
        for (;;)
        {
            std::cout << prompt << " (y/n): ";
            std::string s;
            std::getline(std::cin, s);
            if (s == "y" || s == "Y" || s == "д" || s == "Д") return true;
            if (s == "n" || s == "N" || s == "н" || s == "Н") return false;
        }
    }

    bool CmdEnableUsbMount::runShell(const std::string& cmd, std::string& err)
    {
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::ostringstream oss;
            oss << "Команда завершилась с кодом " << rc;
            err = oss.str();
            return false;
        }
        return true;
    }

    // Astra 1.7+: polkit rules
    std::string CmdEnableUsbMount::rulesPath()
    {
        return "/etc/polkit-1/rules.d/49-astra-mail-usb-mount.rules";
    }

    // Astra 1.6: pkla
    std::string CmdEnableUsbMount::pklaPath()
    {
        return "/etc/polkit-1/localauthority/50-local.d/99-astra-mail-usb-mount.pkla";
    }

    std::string CmdEnableUsbMount::rulesContent()
    {
        // polkit (JS rules), udisks2
        // Разрешаем всем локальным активным пользователям mount через udisks2 без пароля
        return R"(polkit.addRule(function(action, subject) {
    if (action.id == "org.freedesktop.udisks2.filesystem-mount" ||
        action.id == "org.freedesktop.udisks2.filesystem-mount-system") {
        if (subject.isInGroup("users") && subject.active) {
            return polkit.Result.YES;
        }
    }
});
)";
    }

    std::string CmdEnableUsbMount::pklaContent()
    {
        // polkit 0.102 pkla (Astra 1.6)
        // Важно: udisks1 + udisks2, чтобы покрыть оба варианта
        return
                "[Allow mount for all users]\n"
                "Identity=unix-user:*\n"
                "Action=org.freedesktop.udisks.filesystem-mount;org.freedesktop.udisks2.filesystem-mount\n"
                "ResultAny=yes\n"
                "ResultInactive=yes\n"
                "ResultActive=yes\n";
    }

    static std::string shellEscapeSingleQuotes(const std::string& s)
    {
        // превращаем строку в '...'\''...'
        std::string out;
        out.reserve(s.size() + 16);
        for (char c : s)
        {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        return out;
    }

    bool CmdEnableUsbMount::writeFileAsRoot(const std::string& path, const std::string& content, std::string& err)
    {
        const std::string dir = fs::path(path).parent_path().string();

        if (isRoot())
        {
            std::error_code ec;
            fs::create_directories(dir, ec);

            std::ofstream f(path, std::ios::binary);
            if (!f)
            {
                err = "Не удалось открыть файл на запись: " + path;
                return false;
            }
            f << content;
            f.close();
            return true;
        }

        // Не root → пробуем через sudo
        // 1) mkdir -p
        {
            std::string cmd = "sudo -n true >/dev/null 2>&1";
            int hasSudoNoPrompt = (std::system(cmd.c_str()) == 0);

            if (!hasSudoNoPrompt)
            {
                // Будет просить пароль в терминале — это нормально
                std::cout << "\nТребуются права администратора (sudo).\n";
            }
        }

        std::string mk = "sudo mkdir -p '" + shellEscapeSingleQuotes(dir) + "'";
        if (!runShell(mk, err))
            return false;

        // 2) запись через heredoc
        // Важно: используем sudo sh -c "cat > 'path' <<'EOF' ... EOF"
        const std::string marker = "ASTRAMAIL_EOF_7F3A";
        std::ostringstream oss;
        oss << "sudo sh -c \"cat > '" << shellEscapeSingleQuotes(path) << "' <<'" << marker << "'\n"
            << content
            << "\n" << marker << "\n\"";

        if (!runShell(oss.str(), err))
            return false;

        return true;
    }

    CommandResult CmdEnableUsbMount::execute()
    {
        const bool hasRules = dirExists("/etc/polkit-1/rules.d");
        const bool hasPkla  = dirExists("/etc/polkit-1/localauthority");

        std::cout << "Настройка монтирования USB без пароля...\n\n";

        if (!hasRules && !hasPkla)
        {
            std::cout << "Не найдены каталоги polkit (/etc/polkit-1/*).\n"
                         "На этой системе настройка этим способом невозможна.\n";
            pause();
            return CommandResult::Continue;
        }

        const bool useRules = hasRules; // если rules.d есть — считаем это “новым” способом (1.7+)
        const std::string target = useRules ? rulesPath() : pklaPath();

        std::cout << "Режим: " << (useRules ? "Astra 1.7+ (rules.d)" : "Astra 1.6 (pkla)") << "\n"
                  << "Файл: " << target << "\n\n";

        if (fileExists(target))
        {
            if (!askYesNo("Файл уже существует. Перезаписать?"))
            {
                std::cout << "Отменено.\n";
                pause();
                return CommandResult::Continue;
            }
        }
        else
        {
            if (!askYesNo("Создать правило и включить монтирование без пароля?"))
            {
                std::cout << "Отменено.\n";
                pause();
                return CommandResult::Continue;
            }
        }

        std::string err;
        const std::string content = useRules ? rulesContent() : pklaContent();

        if (!writeFileAsRoot(target, content, err))
        {
            std::cout << "Ошибка: " << err << "\n";
            pause();
            return CommandResult::Continue;
        }

        std::cout << "Готово.\n"
                     "Если пароль всё равно спрашивает — перезайди в сессию (logout/login).\n";
        pause();
        return CommandResult::Continue;
    }
}
