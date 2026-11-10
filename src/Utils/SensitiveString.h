#pragma once
#include <string>

namespace astra_mail {
inline void clearSensitiveString(std::string& value) {
    volatile char* data = value.empty() ? nullptr : &value[0];
    for (size_t i = 0; i < value.size(); ++i) data[i] = 0;
    value.clear();
}
struct ClearSensitiveOnExit {
    std::string& value;
    ~ClearSensitiveOnExit() { clearSensitiveString(value); }
};
}
