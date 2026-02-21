#include "menu/MenuCreator.h"
#include "ThunderbirdSettings.h"
#include "menu/StartupProfileLoader.h"
#include "IdleBackground.h"

#include <string>





void runStartup()
{
    astra_mail::TbProfile profile;

    BOOT:
    // 1. Пытаемся загрузить сохранённый профиль
    if (!tryLoadSavedProfile(profile))
    {
        // 2. Если не получилось — даём выбрать профиль
        if (!astra_mail::profileSetupMenu())
            goto BOOT;

        // 3. После выбора ОБЯЗАТЕЛЬНО пробуем загрузить снова
        //    (файл уже должен быть сохранён)
        if (!tryLoadSavedProfile(profile))
            goto BOOT;
    }

    PIN_CHECK:
    // 4. PIN проверяем ТОЛЬКО когда профиль уже есть
    if (!astra_mail::pinMenu())
        goto BOOT;

    CONNECTION_CHECK:
    // 5. Проверка доступности IMAP и SMTP
    if (!astra_mail::checkServers())
        goto PIN_CHECK;

    MAIN_MENU:
    ; // runMainMenu();
}







int main()
{
    using namespace astra_mail;
    setlocale(LC_ALL, "");

    runStartup();

    astra_mail::IdleBackground::instance().start();

    auto  menu = menu::createMainMenu();
    menu->run();

    astra_mail::IdleBackground::instance().stop();
}
