#include "menu/MenuCreator.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "menu/StartupProfileLoader.h"
#include "core/idle/IdleBackground.h"
#include "core/mail/MailNetworkSettings.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "Utils/Utils.h"

#include <string>
#include <cstdio>





bool runStartup()
{
    astra_mail::TbProfile profile;
    while (std::cin.good())
    {
        if (!tryLoadSavedProfile(profile))
        {
            if (!std::cin.good()) return false;
            if (!astra_mail::profileSetupMenu()) continue;
            if (!tryLoadSavedProfile(profile)) continue;
        }
        if (!std::cin.good()) return false;
        if (!astra_mail::pinMenu()) continue;
        if (!std::cin.good()) return false;

        if (!astra_mail::checkServersSilent())
        {
            std::cout << "[Старт] Нет соединения с серверами. Проверку можно выполнить в меню Система 2.\n";
            astra_mail::my::pause();
        }
        return std::cin.good();
    }
    return false;
}

static int inputExitCode()
{
    if (std::cin.bad() || std::ferror(stdin) || (std::cin.fail() && !std::cin.eof()))
    {
        std::cerr << "Ошибка чтения ввода. Программа завершена.\n";
        return 1;
    }
    if (std::cin.eof())
        std::cout << "\nВвод завершён. Программа завершена.\n";
    return 0;
}


int main()
{
    using namespace astra_mail;
    setlocale(LC_ALL, "");

    config::ensureIdleSettings(false);
    net::configure(loadMailNetworkSettings());
    config::ensureLocalMailSettings(false);
    config::ensureLocalContacts(false);

    if (!runStartup()) return inputExitCode();

    astra_mail::IdleBackground::instance().start();

    auto  menu = menu::createMainMenu();
    menu->run();

    astra_mail::IdleBackground::instance().stop();
    return inputExitCode();
}
