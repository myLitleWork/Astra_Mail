#include "Utils.h"

#include <iostream>
#include <limits>

namespace astra_mail
{
    void clearScreen()
    {
        std::cout << "\033[2J\033[1;1H";
    }

    namespace my
    {
        void pause()
        {
            std::cout << "\nНажмите Enter для продолжения...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    uint32_t rnd()
    {
        static thread_local std::mt19937 gen{std::random_device{}()};
        static thread_local std::uniform_int_distribution<uint32_t> dist;
        return dist(gen);
    }

    void playInboxDownloadSound()
    {
        int r = system("paplay /usr/share/sounds/freedesktop/stereo/message.oga 2>/dev/null");
        if (r == 0) return;
        r = system("canberra-gtk-play -i message 2>/dev/null");
        if (r == 0) return;
        r = system("paplay /usr/share/sounds/alsa/Front_Center.wav 2>/dev/null");
        if (r == 0) return;
        std::fprintf(stderr, "\a");
    }
}
