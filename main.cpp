#include "menu/MenuCreator.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "menu/StartupProfileLoader.h"
#include "core/idle/IdleBackground.h"
#include "Configs/Confg/ConfigBootstrap.h"
#include "Utils/Utils.h"

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

    // 5. Проверка подключения — не блокируем вход, только сообщение
    if (!astra_mail::checkServersSilent())
    {
        std::cout << "[Старт] Нет соединения с серверами. Проверку можно выполнить в меню Система 2.\n";
        astra_mail::my::pause();
    }

    MAIN_MENU:
    ; // runMainMenu();
}


int main()
{
    using namespace astra_mail;
    setlocale(LC_ALL, "");

    config::ensureIdleSettings(false);

    runStartup();

    astra_mail::IdleBackground::instance().start();

    auto  menu = menu::createMainMenu();
    menu->run();

    astra_mail::IdleBackground::instance().stop();
}
