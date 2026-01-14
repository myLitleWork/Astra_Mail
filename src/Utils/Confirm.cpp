// Confirm.cpp
#include "Confirm.h"
#include <iostream>

namespace astra_mail
{
    bool confirm(const std::string& question)
    {
        std::cout << question << " [y/N]: ";
        std::cout.flush();

        char c = 0;
        std::cin >> c;

        return c == 'y' || c == 'Y';
    }
}
