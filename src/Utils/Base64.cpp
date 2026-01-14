#include "Base64.h"
#include <cctype>




static inline int b64val(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<unsigned char> decodeBase64(const std::string& in)
{
    std::vector<unsigned char> out;
    int val = 0;
    int valb = -8;

    for (uint8_t c : in)
    {
        if (std::isspace(c))
            continue;

        if (c == '=')
            break;

        int v = b64val(c);
        if (v < 0)
            continue;

        val = (val << 6) + v;
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}
