#pragma once
#include "nebula/storage/sql.hpp"
#include <set>
#include <string>
#include <vector>

namespace nebula::schema {
constexpr int ApplicationId = 0x4e454255; // NEBU
constexpr int Minimum = 1;
constexpr int Current = 10;
// JSON interchange describes canonical records only. Version 10 adds derived
// structures, so its canonical contract is still version 9.
constexpr int InterchangeContract = 9;

inline bool supported(const std::string& version)
{
    for (int v = Minimum; v <= Current; ++v)
        if (version == std::to_string(v))
            return true;
    return false;
}

// Defensive connection settings: Nebula defines no triggers or views, so a
// crafted database cannot run schema code during ordinary saves.
inline void harden(sqlite3* db)
{
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, nullptr);
#ifdef SQLITE_DBCONFIG_ENABLE_VIEW
    sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_VIEW, 0, nullptr);
#endif
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
#endif
    sqlite3_busy_timeout(db, 3000);
}

inline void require_fts5(sqlite3* db)
{
    try {
        sql::execute(db, "CREATE VIRTUAL TABLE temp.nebula_fts5_probe USING fts5(probe); DROP TABLE temp.nebula_fts5_probe");
    } catch (const std::exception&) {
        throw StorageError(ErrorKind::Unsupported,
                           "This SQLite library lacks FTS5, which Nebula search requires; use an SQLite build with FTS5 enabled");
    }
}

// Every table/index any supported version creates, plus FTS5 shadow tables.
inline const std::set<std::string>& known_objects()
{
    static const std::set<std::string> names{
        "workspace_meta", "databases", "pages", "blocks", "property_definitions", "property_values",
        "saved_views", "links", "deleted_blocks", "attachments", "deleted_block_members", "maintenance",
        "page_search", "page_search_data", "page_search_idx", "page_search_content", "page_search_docsize",
        "page_search_config", "page_search_rows", "pages_database", "links_target", "links_source",
        "deleted_blocks_page", "deleted_block_members_block"};
    return names;
}

// Fails closed on triggers, views and unknown objects instead of trusting a
// file's application ID and version alone.
inline void check_objects(sqlite3* db, const std::vector<std::string>& required = {})
{
    sql::Statement objects(db, "SELECT type,name FROM sqlite_schema");
    std::set<std::string> present;
    while (objects.row()) {
        const auto type = objects.text(0), name = objects.text(1);
        if (type == "trigger" || type == "view")
            throw StorageError(ErrorKind::Corrupt, "This workspace contains an unexpected " + type + " (" + name + "); it was not opened");
        const bool internal = name.rfind("sqlite_autoindex_", 0) == 0 || name.rfind("sqlite_stat", 0) == 0;
        if (!internal && !known_objects().count(name))
            throw StorageError(ErrorKind::Corrupt, "This workspace contains an unexpected " + type + " (" + name + "); it was not opened");
        present.insert(name);
    }
    for (const auto& name : required)
        if (!present.count(name))
            throw StorageError(ErrorKind::Corrupt, "This workspace is missing " + name);
}

inline const std::vector<std::string>& current_objects()
{
    static const std::vector<std::string> names{
        "workspace_meta", "databases", "pages", "blocks", "property_definitions", "property_values",
        "saved_views", "links", "deleted_blocks", "attachments", "deleted_block_members", "maintenance",
        "page_search", "page_search_rows", "pages_database", "links_target", "links_source", "deleted_blocks_page"};
    return names;
}
} // namespace nebula::schema
