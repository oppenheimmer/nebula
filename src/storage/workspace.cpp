#include "nebula/storage/workspace.hpp"

#include <sqlite3.h>
#include <stdexcept>
#include <utility>
#include "sql.hpp"
#include "nebula/attachments/files.hpp"
#include "records_internal.hpp"

namespace nebula {
namespace {

constexpr int ApplicationId = 0x4e454255; // NEBU
constexpr int SchemaVersion = 9;

void execute(sqlite3* database, const std::string& sql)
{
    char* error = nullptr;
    if (sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(database);
        sqlite3_free(error);
        throw std::runtime_error("Database operation failed: " + message);
    }
}

std::string scalar(sqlite3* database, const char* sql)
{
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database));
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    if (sqlite3_step(raw) != SQLITE_ROW)
        throw std::runtime_error("Workspace metadata is missing or unreadable");
    const auto* text = sqlite3_column_text(raw, 0);
    return text ? reinterpret_cast<const char*>(text) : "";
}

} // namespace

void DatabaseCloser::operator()(sqlite3* database) const noexcept
{
    if (database)
        sqlite3_close(database);
}

Workspace::Workspace(std::filesystem::path directory, std::string id,
                     std::unique_ptr<sqlite3, DatabaseCloser> database)
    : directory_(std::move(directory)), id_(std::move(id)), database_(std::move(database)) {}

Workspace Workspace::open(const std::filesystem::path& directory)
{
    namespace fs = std::filesystem;
    if (directory.empty() || !directory.is_absolute())
        throw std::invalid_argument("Choose an absolute workspace folder");
    const auto root = fs::weakly_canonical(directory);
    if (fs::exists(root) && !fs::is_directory(root))
        throw std::runtime_error("The workspace location is a file, not a folder");
    fs::create_directories(root);
    const auto attachments = root / "attachments";
    if (fs::is_symlink(attachments))
        throw std::runtime_error("The managed attachments directory must not be a symlink");
    if (fs::exists(attachments) && !fs::is_directory(attachments))
        throw std::runtime_error("The attachments location is a file, not a folder");
    const auto filename = root / "nebula.sqlite3";
    const bool existed = fs::exists(filename);
    if (fs::is_symlink(filename) || (existed && !fs::is_regular_file(filename)))
        throw std::runtime_error("The database location must be a regular file");
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open_v2(filename.u8string().c_str(), &raw,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    std::unique_ptr<sqlite3, DatabaseCloser> database(raw);
    if (opened != SQLITE_OK)
        throw std::runtime_error("Cannot open workspace database: " +
                                 std::string(raw ? sqlite3_errmsg(raw) : "allocation failed"));
    if (sqlite3_db_readonly(raw, "main") != 0)
        throw std::runtime_error("The workspace database is read-only");
    sqlite3_busy_timeout(raw, 3000);
    execute(raw, "PRAGMA foreign_keys=ON");
    execute(raw, "PRAGMA synchronous=FULL");
    execute(raw, "BEGIN IMMEDIATE");
    try {
        const auto application = scalar(raw, "PRAGMA application_id");
        auto version = scalar(raw, "PRAGMA user_version");
        const auto objects = scalar(raw, "SELECT count(*) FROM sqlite_schema");
        if (application == "0" && version == "0" && objects == "0") {
            execute(raw, "PRAGMA application_id=" + std::to_string(ApplicationId));
            execute(raw, "PRAGMA user_version=1");
            execute(raw, "CREATE TABLE workspace_meta (id INTEGER PRIMARY KEY CHECK(id=1),"
                         " workspace_id TEXT NOT NULL UNIQUE)");
            execute(raw, "INSERT INTO workspace_meta VALUES (1, lower(hex(randomblob(16))))");
            version = "1";
        } else if (application != std::to_string(ApplicationId)) {
            throw std::runtime_error("This folder contains a database that is not a Nebula workspace");
        } else if (version != "1" && version != "2" && version != "3" && version != "4" && version != "5" && version != "6" && version != "7" && version != "8" && version != std::to_string(SchemaVersion)) {
            throw std::runtime_error("This workspace format is not supported by this version of Nebula");
        }
        const auto id = scalar(raw, "SELECT workspace_id FROM workspace_meta WHERE id=1");
        if (id.empty())
            throw std::runtime_error("The workspace identity is missing");
        if (version == "1") {
            execute(raw, R"SQL(
                CREATE TABLE databases(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL,
                    trashed INTEGER NOT NULL DEFAULT 0 CHECK(trashed IN (0,1)));
                CREATE TABLE pages(id TEXT PRIMARY KEY NOT NULL,
                    database_id TEXT NOT NULL REFERENCES databases(id), title TEXT NOT NULL,
                    revision INTEGER NOT NULL CHECK(revision>0),
                    trashed INTEGER NOT NULL DEFAULT 0 CHECK(trashed IN (0,1)),
                    created_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')),
                    modified_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
                CREATE INDEX pages_database ON pages(database_id,trashed);
                CREATE TABLE blocks(id TEXT PRIMARY KEY NOT NULL,
                    page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    parent_id TEXT, position INTEGER NOT NULL CHECK(position>=0),
                    kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 5), payload TEXT NOT NULL,
                    UNIQUE(page_id,position), UNIQUE(page_id,id),
                    FOREIGN KEY(page_id,parent_id) REFERENCES blocks(page_id,id)
                        DEFERRABLE INITIALLY DEFERRED);
                CREATE TABLE property_definitions(id TEXT PRIMARY KEY NOT NULL,
                    database_id TEXT NOT NULL REFERENCES databases(id), name TEXT NOT NULL,
                    kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 4), UNIQUE(database_id,name));
                CREATE TABLE property_values(page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    property_id TEXT NOT NULL REFERENCES property_definitions(id), value_json TEXT NOT NULL,
                    PRIMARY KEY(page_id,property_id));
                CREATE TABLE saved_views(id TEXT PRIMARY KEY NOT NULL,
                    database_id TEXT NOT NULL REFERENCES databases(id), name TEXT NOT NULL, settings TEXT NOT NULL);
                CREATE TABLE links(source_page TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    source_block TEXT NOT NULL REFERENCES blocks(id) ON DELETE CASCADE,
                    run_index INTEGER NOT NULL, target_kind INTEGER NOT NULL CHECK(target_kind IN(0,1)),
                    target_id TEXT NOT NULL, PRIMARY KEY(source_block,run_index));
                CREATE INDEX links_target ON links(target_kind,target_id);
                PRAGMA user_version=2;
            )SQL");
            version="2";
        }
        if (version=="2") {
            execute(raw,R"SQL(
                CREATE TABLE deleted_blocks(id TEXT PRIMARY KEY NOT NULL,
                    page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    parent_id TEXT NOT NULL, anchor_id TEXT NOT NULL, fragment TEXT NOT NULL,
                    restored INTEGER NOT NULL DEFAULT 0 CHECK(restored IN(0,1)),
                    removed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
                CREATE INDEX deleted_blocks_page ON deleted_blocks(page_id,restored);
                PRAGMA user_version=3;
            )SQL");
            version="3";
        }
        if (version=="3") {
            execute(raw,R"SQL(
                ALTER TABLE deleted_blocks ADD COLUMN operation_id TEXT NOT NULL DEFAULT '';
                ALTER TABLE deleted_blocks ADD COLUMN original_position INTEGER NOT NULL DEFAULT 0;
                UPDATE deleted_blocks SET operation_id=id;
                PRAGMA user_version=4;
            )SQL");
            version="4";
        }
        if (version=="4") {
            // Rebuild both related tables in the same transaction. Keeping a
            // copied links table avoids cascade deletion of the derived edges.
            execute(raw,R"SQL(
                CREATE TABLE blocks_v5(id TEXT PRIMARY KEY NOT NULL,
                    page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    parent_id TEXT, position INTEGER NOT NULL CHECK(position>=0),
                    kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 6), payload TEXT NOT NULL,
                    UNIQUE(page_id,position), UNIQUE(page_id,id),
                    FOREIGN KEY(page_id,parent_id) REFERENCES blocks_v5(page_id,id)
                        DEFERRABLE INITIALLY DEFERRED);
                INSERT INTO blocks_v5 SELECT * FROM blocks;
                CREATE TABLE links_v5(source_page TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
                    source_block TEXT NOT NULL REFERENCES blocks_v5(id) ON DELETE CASCADE,
                    run_index INTEGER NOT NULL, target_kind INTEGER NOT NULL CHECK(target_kind IN(0,1)),
                    target_id TEXT NOT NULL, PRIMARY KEY(source_block,run_index));
                INSERT INTO links_v5 SELECT * FROM links;
                DROP TABLE links;
                DROP TABLE blocks;
                ALTER TABLE blocks_v5 RENAME TO blocks;
                ALTER TABLE links_v5 RENAME TO links;
                CREATE INDEX links_target ON links(target_kind,target_id);
                PRAGMA user_version=5;
            )SQL");
            version="5";
        }
        if (version=="5") { execute(raw,"PRAGMA user_version=6"); version="6"; }
        if (version=="6") { execute(raw,R"SQL(
            CREATE TABLE attachments(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL,
                size INTEGER NOT NULL CHECK(size>=0), sha256 TEXT NOT NULL CHECK(length(sha256)=64),
                image INTEGER NOT NULL CHECK(image IN(0,1)));
            PRAGMA user_version=7;
        )SQL"); version="7"; }
        if (version=="7") {
            storage_detail::create_search(raw); storage_detail::rebuild_search(raw);
            execute(raw,"PRAGMA user_version=8"); version="8";
        }
        if (version=="8") execute(raw,"PRAGMA user_version=9");
        if (scalar(raw, "PRAGMA quick_check") != "ok")
            throw std::runtime_error("Workspace integrity check failed");
        sql::Statement relationships(raw,"PRAGMA foreign_key_check");
        if (relationships.row()) throw std::runtime_error("Workspace relationships are inconsistent");
        execute(raw,"SELECT id,database_id,title,revision,trashed FROM pages LIMIT 0");
        // Validate the attachment path before committing a newly created database.
        fs::create_directories(attachments);
        files::initialize_lock(root);
        execute(raw, "COMMIT");
        return Workspace(root, id, std::move(database));
    } catch (...) {
        sqlite3_exec(raw, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

} // namespace nebula
