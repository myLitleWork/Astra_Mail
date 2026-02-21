#pragma once
#include <string>
#include <ctime>
#include <climits>
#include <random>

namespace astra_mail
{
    uint32_t rnd();

    void clearScreen();

    /** Воспроизвести звук при скачивании входящих (фон, не блокирует). */
    void playInboxDownloadSound();

    namespace my
    {
        inline std::string formatTime(std::time_t t)
        {
            if (t == 0)
                return "нет данных";

            std::tm tm{};
            localtime_r(&t, &tm);

            char buf[32];
            std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tm);
            return buf;
        }
        void pause();
    }
}
