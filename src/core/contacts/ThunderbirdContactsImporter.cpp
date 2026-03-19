#include <algorithm>
#include <iostream>
#include <unordered_set>
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

        if (!fs::exists(dbPath))
        {
            std::cout << "[TB-IMPORT][WARN] Файл не найден: " << dbPath.string() << "\n";
            return false;
        }

        try
        {
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



            std::cout << "[TB-IMPORT] Выполнение SQL-запроса (контакты)\n";

            auto rows = db->select(sql);

        std::cout << "[TB-IMPORT] Получено контактов: "
                  << rows.size() << "\n";

        bool addedContacts = false;
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
            addedContacts = true;

            std::cout << "[TB-IMPORT][OK] Контакт #" << (i + 1)
                      << ": " << row.at("email") << "\n";
        }

        std::cout << "[TB-IMPORT] Импорт завершён. "
                  << "Добавлено: " << rows.size()
                  << ", дубликатов: " << duplicates << "\n";

        // --- mailing lists (циркулярные записи) ---
        bool addedLists = false;
        size_t listRows = 0;
        try
        {
            // В разных версиях Thunderbird у списка может быть name и/или nickName.
            // Мы сохраняем адреса под обоими ключами (если есть), чтобы папка могла называться псевдонимом.
            const std::string sqlLists =
                    "SELECT l.name AS listName, "
                    "       l.nickName AS listNick, "
                    "       p_email.value AS email "
                    "FROM lists l "
                    // В TB поле list в list_cards хранит uid списка (UUID-строку), а не numeric id.
                    "JOIN list_cards lc ON lc.list = l.uid "
                    "JOIN properties p_email "
                    "  ON p_email.card = lc.card AND p_email.name = 'PrimaryEmail' "
                    "WHERE (l.name IS NOT NULL AND l.name <> '' "
                    "       OR l.nickName IS NOT NULL AND l.nickName <> '') "
                    "  AND p_email.value IS NOT NULL AND p_email.value <> ''";

            std::cout << "[TB-IMPORT] Выполнение SQL-запроса (списки рассылки)\n";
            auto lrows = db->select(sqlLists);
            listRows = lrows.size();

            for (const auto& row : lrows)
            {
                auto itEmail = row.find("email");
                if (itEmail == row.end())
                    continue;

                std::string email = itEmail->second;
                if (email.empty())
                    continue;

                auto addKey = [&](const char* col)
                {
                    auto it = row.find(col);
                    if (it == row.end()) return;
                    std::string key = normalizeKey(it->second);
                    if (key.empty()) return;
                    mailingLists_[key].push_back(email);
                    addedLists = true;
                };

                addKey("listName");
                addKey("listNick");
            }

            // дедупликация email-ов в каждом списке
            for (auto& kv : mailingLists_)
            {
                auto& v = kv.second;
                std::unordered_set<std::string> seen;
                std::vector<std::string> out;
                out.reserve(v.size());
                for (auto& e : v)
                {
                    if (seen.insert(e).second)
                        out.push_back(std::move(e));
                }
                v.swap(out);
            }
        }
        catch (...)
        {
            // В некоторых версиях Thunderbird таблиц lists/list_cards или колонок может не быть.
        }

        if (addedLists)
            std::cout << "[TB-IMPORT] Списков рассылки: " << mailingLists_.size()
                      << ", строк: " << listRows << "\n";

        if (!addedContacts && !addedLists)
        {
            std::cout << "[TB-IMPORT][WARN] Контакты/списки не найдены\n";
            return false;
        }

            return true;
        }
        catch (const std::exception& e)
        {
            std::cout << "[TB-IMPORT][ERROR] Ошибка открытия/чтения БД: " << e.what() << "\n";
            return false;
        }
        catch (...)
        {
            std::cout << "[TB-IMPORT][ERROR] Неизвестная ошибка при чтении БД\n";
            return false;
        }
    }




    bool ThunderbirdContactsImporter::hasDuplicateKeys() const
    {
        return hasDuplicates_;
    }




    const std::unordered_map<std::string, astra_mail::Contact> &
    ThunderbirdContactsImporter::contacts() const {
        return contacts_;
    }

    const std::unordered_map<std::string, std::vector<std::string>>&
    ThunderbirdContactsImporter::mailingLists() const
    {
        return mailingLists_;
    }


}