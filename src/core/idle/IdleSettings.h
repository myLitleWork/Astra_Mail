#pragma once

#include <cstdint>

namespace astra_mail
{
    struct IdleSettingsValues
    {
        uint32_t idleThresholdSeconds = 300;
        uint32_t checkIntervalSeconds = 30;
    };

    /** Загружает настройки из файла. При ошибке возвращает значения по умолчанию. */
    IdleSettingsValues loadIdleSettings();
}
