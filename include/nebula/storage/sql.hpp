#pragma once
// Narrow SQLite helpers shared by storage and the interchange adapters. This is
// an internal module API: application and desktop code use Workspace instead.
#include "nebula/storage/errors.hpp"
#include <cstdint>
#include <memory>
#include <sqlite3.h>
#include <string>

namespace nebula::sql {

inline ErrorKind kind_of(int code)
{
    switch (code & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return ErrorKind::Busy;
    case SQLITE_CONSTRAINT:
    case SQLITE_MISMATCH:
    case SQLITE_RANGE:
    case SQLITE_TOOBIG:
        return ErrorKind::Invalid;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
        return ErrorKind::Corrupt;
    case SQLITE_CANTOPEN:
    case SQLITE_NOTFOUND:
        return ErrorKind::Unavailable;
    default:
        return ErrorKind::Io;
    }
}

[[noreturn]] inline void fail(sqlite3* db, int code, const std::string& prefix = {})
{
    throw StorageError(kind_of(code), prefix + (db ? sqlite3_errmsg(db) : sqlite3_errstr(code)));
}

inline void execute(sqlite3* db, const std::string& command)
{
    char* error = nullptr;
    const int code = sqlite3_exec(db, command.c_str(), nullptr, nullptr, &error);
    if (code != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw StorageError(kind_of(code), message);
    }
}

class Statement {
    sqlite3* db_;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_{nullptr, sqlite3_finalize};

public:
    Statement(sqlite3* db, const char* command) : db_(db)
    {
        sqlite3_stmt* raw = nullptr;
        const int code = sqlite3_prepare_v2(db, command, -1, &raw, nullptr);
        if (code != SQLITE_OK)
            fail(db, code);
        stmt_.reset(raw);
    }
    Statement(sqlite3* db, const std::string& command) : Statement(db, command.c_str()) {}
    Statement& bind(int index, const std::string& value)
    {
        const int code = sqlite3_bind_text(stmt_.get(), index, value.data(), int(value.size()), SQLITE_TRANSIENT);
        if (code != SQLITE_OK)
            fail(db_, code);
        return *this;
    }
    Statement& bind(int index, std::int64_t value)
    {
        const int code = sqlite3_bind_int64(stmt_.get(), index, value);
        if (code != SQLITE_OK)
            fail(db_, code);
        return *this;
    }
    Statement& bind_null(int index)
    {
        const int code = sqlite3_bind_null(stmt_.get(), index);
        if (code != SQLITE_OK)
            fail(db_, code);
        return *this;
    }
    // Reuse a prepared statement in a loop instead of preparing it per row.
    Statement& reset()
    {
        sqlite3_reset(stmt_.get());
        sqlite3_clear_bindings(stmt_.get());
        return *this;
    }
    int type(int column) const { return sqlite3_column_type(stmt_.get(), column); }
    bool row()
    {
        const int status = sqlite3_step(stmt_.get());
        if (status == SQLITE_ROW)
            return true;
        if (status == SQLITE_DONE)
            return false;
        fail(db_, sqlite3_extended_errcode(db_));
    }
    void done()
    {
        if (row())
            throw StorageError(ErrorKind::Io, "Unexpected database result");
    }
    std::string text(int column) const
    {
        const auto* value = sqlite3_column_text(stmt_.get(), column);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(stmt_.get(), column))
                     : "";
    }
    std::int64_t number(int column) const { return sqlite3_column_int64(stmt_.get(), column); }
    double real(int column) const { return sqlite3_column_double(stmt_.get(), column); }
};

inline std::string scalar(sqlite3* db, const char* command)
{
    Statement query(db, command);
    if (!query.row())
        throw StorageError(ErrorKind::Corrupt, "Database result is missing");
    return query.text(0);
}

// A transaction opened while another is active becomes a savepoint, so storage
// operations compose (for example a fixture builder or a multi-step command).
class Transaction {
    sqlite3* db_;
    bool committed_ = false;
    std::string savepoint_;

public:
    explicit Transaction(sqlite3* db) : db_(db)
    {
        if (!sqlite3_get_autocommit(db)) {
            static int counter = 0;
            savepoint_ = "nebula_" + std::to_string(++counter);
            execute(db, "SAVEPOINT " + savepoint_);
        } else {
            execute(db, "BEGIN IMMEDIATE");
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction()
    {
        if (committed_)
            return;
        if (savepoint_.empty())
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        else
            sqlite3_exec(db_, ("ROLLBACK TO " + savepoint_ + "; RELEASE " + savepoint_).c_str(), nullptr, nullptr,
                         nullptr);
    }
    void commit()
    {
        execute(db_, savepoint_.empty() ? std::string("COMMIT") : "RELEASE " + savepoint_);
        committed_ = true;
    }
};

inline std::string id(sqlite3* db)
{
    return scalar(db, "SELECT lower(hex(randomblob(16)))");
}

inline void changed(sqlite3* db)
{
    if (sqlite3_changes(db) != 1)
        throw StorageError(ErrorKind::Conflict,
                           "Record is missing or changed in another window; reopen it before saving");
}

} // namespace nebula::sql
