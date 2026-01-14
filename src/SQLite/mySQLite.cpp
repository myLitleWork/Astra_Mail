#include "mySQLite.h"


int mySQLite::callback(void *data, int argc, char **argv, char **azColName) {
    auto *self = static_cast<mySQLite*>(data);
    std::lock_guard<std::mutex> lock(self->listMutex);

    self->list.emplace_back();
    auto &back = self->list.back();
    for (int i = 0; i < argc; i++) {
        back[azColName[i]] = argv[i] ? argv[i] : "";
    }
    return 0;
}

mySQLite::mySQLite(const std::string& base_dir, MOD mod) {


    switch (mod)
    {

        case MOD::READ_AND_WRITE :
            connect(base_dir);
            break;
        case MOD::READ:
            connectRead(base_dir);
    }


}

mySQLite::~mySQLite() {
    close();
}

void mySQLite::connect(const std::string& base_dir) {
    std::lock_guard<std::mutex> lock(dbMutex);
    dir = base_dir;

    if (db) close();

    int rc = sqlite3_open(dir.c_str(), &db);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("Failed to open database: " + dir);
    }
    sqlite3_busy_timeout(db, 3000);
}

void mySQLite::connectRead(const std::string& base_dir)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    dir = base_dir;

    if (db) close();

    int rc = sqlite3_open_v2(
            dir.c_str(),
            &db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
            nullptr
    );

    if (rc != SQLITE_OK)
    {
        sqlite3_close(db);
        db = nullptr;
        throw std::runtime_error("Failed to open database: " + dir);
    }

    // WAL-aware, только чтение
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA read_uncommitted=1;", nullptr, nullptr, nullptr);
}


void mySQLite::close() {
    std::lock_guard<std::mutex> lock(dbMutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

void mySQLite::execSql(const std::string& _dir, const std::string& _sql) {
    std::lock_guard<std::mutex> lock(dbMutex);
    close();
    connect(_dir);
    execSql(_sql);
    close();
    connect(this->dir); // Reconnect to the original directory
}

const mySQLite::Row& mySQLite::getFront() const {
    std::lock_guard<std::mutex> lock(listMutex);
    return list.front();
}

const mySQLite::Row& mySQLite::getBack() const {
    std::lock_guard<std::mutex> lock(listMutex);
    return list.back();
}

bool mySQLite::empty() const {
    std::lock_guard<std::mutex> lock(listMutex);
    return list.empty();
}

size_t mySQLite::size() const {
    std::lock_guard<std::mutex> lock(listMutex);
    return list.size();
}

void mySQLite::execSql(const std::string& sql)
{
    std::lock_guard<std::mutex> lock(dbMutex);
    list.clear();

    int rc = sqlite3_exec(db, sql.c_str(), callback, this, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::string errMsg = zErrMsg;
        sqlite3_free(zErrMsg);
        throw std::runtime_error("SQL execution failed: " + errMsg);
    }

    /* курсор ставим на начало свежего результата */
    std::lock_guard<std::mutex> l(listMutex);
    cur_ = list.begin();
}

void mySQLite::first()
{
    std::lock_guard<std::mutex> l(listMutex);
    cur_ = list.begin();
}

void mySQLite::next()
{
    std::lock_guard<std::mutex> l(listMutex);
    if (cur_ != list.end()) ++cur_;
}

bool mySQLite::eof() const
{
    std::lock_guard<std::mutex> l(listMutex);
    return cur_ == list.end();
}

const mySQLite::Row& mySQLite::current() const
{
    std::lock_guard<std::mutex> l(listMutex);
    if (cur_ == list.end())
        throw std::out_of_range("cursor is at end");
    return *cur_;
}

const std::string& mySQLite::value(const std::string& field) const
{
    return current().at(field);   // если нет поля — std::out_of_range
}

/* итераторы только-чтение, потокобезопасность на совести вызывающего */
mySQLite::const_iterator mySQLite::begin() const { return list.cbegin(); }
mySQLite::const_iterator mySQLite::end()   const { return list.cend(); }


void mySQLite::backupTo(const std::string& destPath)
{
    std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3* dst = nullptr;
    int rc = sqlite3_open(destPath.c_str(), &dst);
    if (rc != SQLITE_OK) {
        std::string msg = "Не удалось открыть целевой файл: ";
        msg += sqlite3_errmsg(dst);
        sqlite3_close(dst);
        throw std::runtime_error(msg);
    }

    sqlite3_backup* backup = sqlite3_backup_init(dst, "main", static_cast<sqlite3*>(db), "main");
    if (!backup) {
        std::string msg = "Ошибка backup_init: ";
        msg += sqlite3_errmsg(static_cast<sqlite3*>(db));
        sqlite3_close(dst);
        throw std::runtime_error(msg);
    }

    rc = sqlite3_backup_step(backup, -1);
    if (rc != SQLITE_DONE && rc != SQLITE_OK) {
        std::string msg = "Ошибка backup_step: ";
        msg += sqlite3_errmsg(dst);
        sqlite3_backup_finish(backup);
        sqlite3_close(dst);
        throw std::runtime_error(msg);
    }

    rc = sqlite3_backup_finish(backup);
    if (rc != SQLITE_OK) {
        std::string msg = "Ошибка backup_finish: ";
        msg += sqlite3_errmsg(dst);
        sqlite3_close(dst);
        throw std::runtime_error(msg);
    }

    sqlite3_close(dst);
}



std::vector<mySQLite::Row> mySQLite::select(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex);

    std::vector<Row> rows;

    sqlite3_stmt* stmt = nullptr;
    const int rcPrep = sqlite3_prepare_v2(static_cast<sqlite3*>(db), sql.c_str(), -1, &stmt, nullptr);
    if (rcPrep != SQLITE_OK) {
        // можно бросить исключение или вернуть пустой результат
        return rows;
    }

    const int colCount = sqlite3_column_count(stmt);
    std::vector<std::string> colNames;
    colNames.reserve(colCount);
    for (int i = 0; i < colCount; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        colNames.emplace_back(name ? name : "");
    }

    while (true) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            Row row;
            for (int i = 0; i < colCount; ++i) {
                const unsigned char* txt = sqlite3_column_text(stmt, i);
                row[colNames[i]] = txt ? reinterpret_cast<const char*>(txt) : "";
            }
            rows.emplace_back(std::move(row));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            // ошибка — прерываем и возвращаем то, что успели
            break;
        }
    }

    sqlite3_finalize(stmt);
    return rows;
}


