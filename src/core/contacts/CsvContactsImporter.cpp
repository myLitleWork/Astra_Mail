#include "CsvContactsImporter.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace astra_mail
{
    static std::vector<std::string> parseCsvLine(const std::string& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool inQuotes = false;

        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];

            if (inQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < line.size() && line[i + 1] == '"')
                    {
                        field.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    field.push_back(c);
                }
            }
            else
            {
                if (c == '"')
                {
                    inQuotes = true;
                }
                else if (c == ',')
                {
                    fields.push_back(field);
                    field.clear();
                }
                else
                {
                    field.push_back(c);
                }
            }
        }
        fields.push_back(field);
        return fields;
    }

    static std::string normalizeKey(std::string s)
    {
        auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isWs));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), isWs).base(), s.end());

        std::string out;
        out.reserve(s.size());
        bool prevUnderscore = false;

        for (unsigned char c : s)
        {
            if (std::isspace(c))
            {
                if (!out.empty() && !prevUnderscore)
                {
                    out.push_back('_');
                    prevUnderscore = true;
                }
            }
            else
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                prevUnderscore = false;
            }
        }

        if (!out.empty() && out.back() == '_')
            out.pop_back();

        return out;
    }

    static int findColumn(const std::vector<std::string>& header, const char* name)
    {
        for (size_t i = 0; i < header.size(); ++i)
        {
            if (header[i] == name)
                return static_cast<int>(i);
        }
        return -1;
    }

    static std::string getField(const std::vector<std::string>& row, int col)
    {
        if (col < 0 || col >= static_cast<int>(row.size()))
            return {};
        return row[static_cast<size_t>(col)];
    }

    bool CsvContactsImporter::loadFromFile(const fs::path& csvPath)
    {
        contacts_.clear();
        skipped_ = 0;

        std::ifstream in(csvPath.string());
        if (!in.is_open())
        {
            std::cout << "[CSV-IMPORT][ERROR] Не удалось открыть: " << csvPath.string() << "\n";
            return false;
        }

        std::string headerLine;
        if (!std::getline(in, headerLine))
        {
            std::cout << "[CSV-IMPORT][ERROR] Файл пуст\n";
            return false;
        }

        auto header = parseCsvLine(headerLine);

        const int colDisplayName = findColumn(header, "Отображаемое имя");
        const int colNickName    = findColumn(header, "Псевдоним");
        const int colEmail       = findColumn(header, "Адрес электронной почты");

        if (colEmail < 0)
        {
            std::cout << "[CSV-IMPORT][ERROR] Не найдена колонка 'Адрес электронной почты'\n";
            return false;
        }

        std::string line;
        size_t lineNum = 1;

        while (std::getline(in, line))
        {
            ++lineNum;
            if (line.empty())
                continue;

            auto fields = parseCsvLine(line);

            std::string email       = getField(fields, colEmail);
            std::string displayName = getField(fields, colDisplayName);
            std::string nickName    = getField(fields, colNickName);

            if (email.empty())
            {
                ++skipped_;
                continue;
            }

            if (nickName.empty() && !displayName.empty())
                nickName = displayName;

            if (nickName.empty())
            {
                auto at = email.find('@');
                nickName = (at != std::string::npos) ? email.substr(0, at) : email;
            }

            Contact c;
            c.email = email;
            c.displayName = displayName;
            c.nickName = nickName;
            c.key = normalizeKey(nickName);

            if (c.key.empty())
            {
                ++skipped_;
                continue;
            }

            contacts_[c.key] = c;

            std::cout << "[CSV-IMPORT][OK] " << c.nickName
                      << " <" << c.email << ">\n";
        }

        std::cout << "[CSV-IMPORT] Загружено: " << contacts_.size()
                  << ", пропущено: " << skipped_ << "\n";

        return !contacts_.empty();
    }

    const std::unordered_map<std::string, Contact>& CsvContactsImporter::contacts() const
    {
        return contacts_;
    }

    size_t CsvContactsImporter::skippedCount() const
    {
        return skipped_;
    }
}
