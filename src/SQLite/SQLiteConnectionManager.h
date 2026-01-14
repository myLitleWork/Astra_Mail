#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "mySQLite.h"

class SQLiteConnectionManager {
public:
    static SQLiteConnectionManager& instance();

    std::shared_ptr<mySQLite> getConnection(const std::string& dbPath, mySQLite::MOD mod = mySQLite::MOD::READ_AND_WRITE);
    void closeConnection(const std::string& dbPath);

private:
    std::map<std::string, std::shared_ptr<mySQLite>> connections_;
    std::mutex mutex_;

    SQLiteConnectionManager();
    SQLiteConnectionManager(const SQLiteConnectionManager&) = delete;
    SQLiteConnectionManager& operator=(const SQLiteConnectionManager&) = delete;
};
