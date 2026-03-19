#pragma once

#include "UtilsFS/FsCompat.h"
#include "core/contacts/ThunderbirdContactsImporter.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace astra_mail
{
    class CsvContactsImporter
    {
    public:
        bool loadFromFile(const fs::path& csvPath);

        const std::unordered_map<std::string, Contact>& contacts() const;
        size_t skippedCount() const;

    private:
        std::unordered_map<std::string, Contact> contacts_;
        size_t skipped_ = 0;
    };
}
