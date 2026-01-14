#pragma once
#include <cstdint>

enum class MailSaveFlags : uint32_t
{
    None        = 0,
    SaveEml     = 1 << 0,
    SaveAttach  = 1 << 1,
    SaveBody    = 1 << 2,   // пока не используем
    SortByTopic = 1 << 3,
};

inline MailSaveFlags operator|(MailSaveFlags a, MailSaveFlags b)
{
    return static_cast<MailSaveFlags>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
    );
}

inline bool hasFlag(MailSaveFlags f, MailSaveFlags bit)
{
    return (static_cast<uint32_t>(f) & static_cast<uint32_t>(bit)) != 0;
}
