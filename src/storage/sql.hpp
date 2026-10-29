#pragma once
#include <sqlite3.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace nebula::sql {
inline void execute(sqlite3* db, const std::string& command) {
    char* error = nullptr;
    if (sqlite3_exec(db, command.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error); throw std::runtime_error(message);
    }
}
class Statement {
    sqlite3* db_;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_{nullptr, sqlite3_finalize};
public:
    Statement(sqlite3* db, const char* command) : db_(db) {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db, command, -1, &raw, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
        stmt_.reset(raw);
    }
    Statement& bind(int index, const std::string& value) {
        if (sqlite3_bind_text(stmt_.get(), index, value.data(), int(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        return *this;
    }
    Statement& bind(int index, std::int64_t value) {
        if (sqlite3_bind_int64(stmt_.get(), index, value) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
        return *this;
    }
    bool row() {
        const int status = sqlite3_step(stmt_.get());
        if (status == SQLITE_ROW) return true;
        if (status == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void done() { if (row()) throw std::runtime_error("Unexpected database result"); }
    std::string text(int column) const {
        const auto* value = sqlite3_column_text(stmt_.get(), column);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(stmt_.get(), column)) : "";
    }
    std::int64_t number(int column) const { return sqlite3_column_int64(stmt_.get(), column); }
};
inline std::string scalar(sqlite3* db, const char* command) {
    Statement query(db, command);
    if (!query.row()) throw std::runtime_error("Database result is missing");
    return query.text(0);
}
class Transaction {
    sqlite3* db_;
    bool committed_ = false;
public:
    explicit Transaction(sqlite3* db) : db_(db) { execute(db, "BEGIN IMMEDIATE"); }
    ~Transaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { execute(db_, "COMMIT"); committed_ = true; }
};
inline std::string id(sqlite3* db) { return scalar(db, "SELECT lower(hex(randomblob(16)))"); }
inline void changed(sqlite3* db) { if (sqlite3_changes(db) != 1) throw std::runtime_error("Record is missing or changed in another window; reopen it before saving"); }
} // namespace nebula::sql
