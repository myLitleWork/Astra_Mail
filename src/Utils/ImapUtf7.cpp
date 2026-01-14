#include "ImapUtf7.h"
#include <vector>
#include <sstream>
#include <cstdint>

namespace astra_mail::imap {

    static std::string base64DecodeModified(const std::string& in)
    {
        static const std::string b64 =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

        std::vector<uint8_t> out;
        int val = 0, valb = -8;

        for (unsigned char c : in)
        {
            if (c == '-') break;
            int idx = b64.find(c);
            if (idx == std::string::npos) continue;

            val = (val << 6) + idx;
            valb += 6;

            if (valb >= 0)
            {
                out.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }

        // UTF-16BE → UTF-8
        std::string utf8;
        for (size_t i = 0; i + 1 < out.size(); i += 2)
        {
            uint16_t ch = (out[i] << 8) | out[i + 1];

            if (ch < 0x80)
                utf8.push_back(static_cast<char>(ch));
            else if (ch < 0x800)
            {
                utf8.push_back(0xC0 | (ch >> 6));
                utf8.push_back(0x80 | (ch & 0x3F));
            }
            else
            {
                utf8.push_back(0xE0 | (ch >> 12));
                utf8.push_back(0x80 | ((ch >> 6) & 0x3F));
                utf8.push_back(0x80 | (ch & 0x3F));
            }
        }

        return utf8;
    }

    std::string decodeUtf7(const std::string& s)
    {
        std::string out;

        for (size_t i = 0; i < s.size();)
        {
            if (s[i] == '&')
            {
                auto end = s.find('-', i);
                if (end != std::string::npos && end > i + 1)
                {
                    out += base64DecodeModified(s.substr(i + 1, end - i - 1));
                    i = end + 1;
                }
                else
                {
                    out += '&';
                    ++i;
                }
            }
            else
            {
                out += s[i++];
            }
        }

        return out;
    }

}
