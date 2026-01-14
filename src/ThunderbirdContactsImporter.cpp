#include <algorithm>
#include <iostream>
#include "ThunderbirdContactsImporter.h"
#include "SQLiteConnectionManager.h"


namespace astra_mail {


    static std::string makeContactKey(const Contact& c)
    {
        if (!c.nickName.empty())
            return c.nickName;

        if (!c.displayName.empty())
            return c.displayName;

        return c.email;
    }

    std::string normalizeKey(std::string s)
    {
        // trim (любой whitespace)
        auto is_ws = [](unsigned char c){ return std::isspace(c); };

        s.erase(s.begin(),
                std::find_if_not(s.begin(), s.end(), is_ws));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), is_ws).base(),
                s.end());

        // collapse spaces -> '_' + tolower
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

        // правый trim '_'
        if (!out.empty() && out.back() == '_')
            out.pop_back();

        return out;
    }




    bool ThunderbirdContactsImporter::loadOneBook(const fs::path& dbPath)
    {
        std::cout << "[TB-IMPORT] Открытие адресной книги: "
                  << dbPath.string() << "\n";

        auto db = SQLiteConnectionManager::instance()
                .getConnection(dbPath.string(), mySQLite::MOD::READ);

        if (!db)
        {
            std::cout << "[TB-IMPORT][ERROR] Не удалось открыть БД\n";
            return false;
        }


        const std::string sql =
                "SELECT p_email.value AS email, "
                "       p_disp.value  AS displayName, "
                "       p_nick.value  AS nickName "
                "FROM properties p_email "
                "LEFT JOIN properties p_disp "
                "  ON p_disp.card = p_email.card AND p_disp.name = 'DisplayName' "
                "LEFT JOIN properties p_nick "
                "  ON p_nick.card = p_email.card AND p_nick.name = 'NickName' "
                "WHERE p_email.name = 'PrimaryEmail' "
                "  AND p_email.value IS NOT NULL "
                "  AND p_email.value <> ''";



        std::cout << "[TB-IMPORT] Выполнение SQL-запроса\n";

        auto rows = db->select(sql);

        std::cout << "[TB-IMPORT] Получено строк: "
                  << rows.size() << "\n";

        if (rows.empty())
        {
            std::cout << "[TB-IMPORT][WARN] Контакты не найдены\n";
            return false;
        }

        bool added = false;
        size_t duplicates = 0;

        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& row = rows[i];

            Contact c;
            c.email = row.at("email");

            auto itDisp = row.find("displayName");
            if (itDisp != row.end())
                c.displayName = itDisp->second;

            auto itNick = row.find("nickName");
            if (itNick != row.end())
                c.nickName = itNick->second;

            c.key = normalizeKey(makeContactKey(c));

            if (contacts_.find(c.key) != contacts_.end())
            {
                hasDuplicates_ = true;
                ++duplicates;

                std::cout << "[TB-IMPORT][DUPLICATE] "
                          << c.email << " (key=" << c.key << ")\n";
            }

            contacts_[c.key] = std::move(c);
            added = true;

            std::cout << "[TB-IMPORT][OK] Контакт #" << (i + 1)
                      << ": " << row.at("email") << "\n";
        }

        std::cout << "[TB-IMPORT] Импорт завершён. "
                  << "Добавлено: " << rows.size()
                  << ", дубликатов: " << duplicates << "\n";

        return added;
    }




    bool ThunderbirdContactsImporter::hasDuplicateKeys() const
    {
        return hasDuplicates_;
    }




    const std::unordered_map<std::string, astra_mail::Contact> &
    ThunderbirdContactsImporter::contacts() const {
        return contacts_;
    }


}