#pragma once
#include <cstddef>
#include <string>

namespace astra_mail {
// Reads one complete line without trimming. A terminal must support hiding
// input; pipes have no terminal echo. Empty/incomplete/oversized input fails.
bool readSecret(const char* prompt, std::string& value, size_t maximum = 4096);
bool validNewPin(const std::string& value);
}
