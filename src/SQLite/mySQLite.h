#pragma once
#include <string>
#include <vector>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include "sqlite3.h"

class mySQLite {

    sqlite3 *db{nullptr};
    std::string dir;
    char *zErrMsg{nullptr};
    mutable std::mutex dbMutex, listMutex;

    using Row = std::unordered_map<std::string, std::string>;
    std::list<Row> list;

    static int callback(void *data, int argc, char **argv, char **azColName);

public:

    enum class MOD
    {
        READ_AND_WRITE,
        READ
    };


    explicit mySQLite(const std::string& base_dir, MOD mod = MOD::READ_AND_WRITE);
    ~mySQLite();

    void execSql(const std::string& sql);
    void execSql(const std::string& dir, const std::string& sql);
    std::vector<Row> select(const std::string& sql);

    const Row& getFront() const;
    const Row& getBack() const;

    void backupTo(const std::string& destPath);

    bool empty() const;
    size_t size() const;
    sqlite3* handle() const noexcept { return static_cast<sqlite3*>(db); }


    using RowList          = std::list<Row>;
    using const_iterator   = RowList::const_iterator;

    /* --- «квери-подобные» новинки --- */
    void first();                 // курсор → первый ряд
    void next();                  // курсор++
    [[nodiscard]] bool eof() const;  // достигнут конец?
    [[nodiscard]] const Row& current() const; // текущий ряд

    /* STL-итераторы (для range-for) */
    [[nodiscard]] const_iterator begin() const;
    [[nodiscard]] const_iterator end() const;

    /* Быстрый доступ к полю текущего ряда */
    [[nodiscard]] const std::string& value(const std::string& field) const;

private:
    void connect(const std::string& base_dir);
    void connectRead(const std::string& base_dir);
    void close();

    /* новый член-состояние курсора */
    const_iterator                cur_{list.cend()};
};
