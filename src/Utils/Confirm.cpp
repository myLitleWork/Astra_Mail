// Confirm.cpp
#include "Confirm.h"
#include "ConsoleInput.h"
#include <iostream>

namespace astra_mail
{
    bool confirm(const std::string& question)
    {
        std::cout << question << " [y/N]: ";
        std::cout.flush();

        std::string answer;
        if (!readConsoleLine(answer)) return false;
        const auto first = answer.find_first_not_of(" \t\r");
        return first != std::string::npos && (answer[first] == 'y' || answer[first] == 'Y');
    }
}
