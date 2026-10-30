#include "nebula/storage/workspace.hpp"

#include "nebula/attachments/files.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/storage/internals.hpp"
#include "nebula/storage/sql.hpp"
#include "records_internal.hpp"
#include "schema.hpp"
#include <QDateTime>
#include <functional>
#include <sqlite3.h>
#include <stdexcept>
#include <utility>

namespace nebula {
namespace {

using sql::execute;

void migrate_to_2(sqlite3* raw)
{
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
    )SQL");
}
void migrate_to_3(sqlite3* raw)
{
    execute(raw, R"SQL(
        CREATE TABLE deleted_blocks(id TEXT PRIMARY KEY NOT NULL,
            page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
            parent_id TEXT NOT NULL, anchor_id TEXT NOT NULL, fragment TEXT NOT NULL,
            restored INTEGER NOT NULL DEFAULT 0 CHECK(restored IN(0,1)),
            removed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
        CREATE INDEX deleted_blocks_page ON deleted_blocks(page_id,restored);
    )SQL");
}
void migrate_to_4(sqlite3* raw)
{
    execute(raw, R"SQL(
        ALTER TABLE deleted_blocks ADD COLUMN operation_id TEXT NOT NULL DEFAULT '';
        ALTER TABLE deleted_blocks ADD COLUMN original_position INTEGER NOT NULL DEFAULT 0;
        UPDATE deleted_blocks SET operation_id=id;
    )SQL");
}
void migrate_to_5(sqlite3* raw)
{
    // Rebuild both related tables in the same transaction. Keeping a copied
    // links table avoids cascade deletion of the derived edges.
    execute(raw, R"SQL(
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
        INSERT INTO links_v5 SELECT source_page,source_block,run_index,target_kind,target_id FROM links;
        DROP TABLE links;
        DROP TABLE blocks;
        ALTER TABLE blocks_v5 RENAME TO blocks;
        ALTER TABLE links_v5 RENAME TO links;
        CREATE INDEX links_target ON links(target_kind,target_id);
    )SQL");
}
void migrate_to_6(sqlite3*) {}
void migrate_to_7(sqlite3* raw)
{
    execute(raw, R"SQL(
        CREATE TABLE attachments(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL,
            size INTEGER NOT NULL CHECK(size>=0), sha256 TEXT NOT NULL CHECK(length(sha256)=64),
            image INTEGER NOT NULL CHECK(image IN(0,1)));
    )SQL");
}
void migrate_to_8(sqlite3* raw)
{
    storage_detail::create_search(raw);
}
void migrate_to_9(sqlite3*) {}
void migrate_to_10(sqlite3* raw)
{
    // Derived structures only: indexed reverse links, recovery membership,
    // maintenance timestamps and the page/block search layout.
    execute(raw, R"SQL(
        CREATE INDEX IF NOT EXISTS links_source ON links(source_page);
        CREATE TABLE IF NOT EXISTS deleted_block_members(
            fragment_id TEXT NOT NULL REFERENCES deleted_blocks(id) ON DELETE CASCADE,
            block_id TEXT NOT NULL, PRIMARY KEY(fragment_id,block_id));
        CREATE INDEX IF NOT EXISTS deleted_block_members_block ON deleted_block_members(block_id);
        CREATE TABLE IF NOT EXISTS maintenance(name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);
    )SQL");
    WorkspaceInternals::rebuild_recovery_members(raw);
    storage_detail::create_search(raw);
}

struct Migration {
    int to;
    void (*apply)(sqlite3*);
};
const Migration migrations[] = {{2, migrate_to_2}, {3, migrate_to_3}, {4, migrate_to_4},
                                {5, migrate_to_5}, {6, migrate_to_6}, {7, migrate_to_7},
                                {8, migrate_to_8}, {9, migrate_to_9}, {10, migrate_to_10}};
static_assert(sizeof(migrations) / sizeof(migrations[0]) == schema::Current - schema::Minimum,
              "Every supported version needs one migration step");

std::vector<std::string> integrity_problems(sqlite3* raw)
{
    std::vector<std::string> problems;
    sql::Statement check(raw, "PRAGMA quick_check");
    while (check.row())
        if (check.text(0) != "ok")
            problems.push_back("SQLite integrity: " + check.text(0));
    sql::Statement relationships(raw, "PRAGMA foreign_key_check");
    while (relationships.row())
        problems.push_back("Relationship from " + relationships.text(0) + " to " + relationships.text(2) +
                           " is missing");
    return problems;
}
void record_check(sqlite3* raw)
{
    sql::Statement stamp(
        raw, "INSERT INTO maintenance(name,value) VALUES('integrity_checked_at',strftime('%Y-%m-%dT%H:%M:%fZ','now')) "
             "ON CONFLICT(name) DO UPDATE SET value=excluded.value");
    stamp.done();
}
bool check_due(sqlite3* raw)
{
    sql::Statement last(raw, "SELECT value FROM maintenance WHERE name='integrity_checked_at'");
    if (!last.row())
        return true;
    const auto checked = QDateTime::fromString(QString::fromStdString(last.text(0)), Qt::ISODateWithMs);
    return !checked.isValid() || checked.addDays(7) < QDateTime::currentDateTimeUtc();
}

} // namespace

void DatabaseCloser::operator()(sqlite3* database) const noexcept
{
    if (database)
        sqlite3_close(database);
}

Workspace::Workspace(std::filesystem::path directory, std::string id, std::unique_ptr<sqlite3, DatabaseCloser> database)
    : directory_(std::move(directory)), id_(std::move(id)), database_(std::move(database))
{
}

int WorkspaceInternals::schema_version(sqlite3* database)
{
    return std::stoi(sql::scalar(database, "PRAGMA user_version"));
}

int WorkspaceInternals::current_schema()
{
    return schema::Current;
}
int WorkspaceInternals::interchange_contract()
{
    return schema::InterchangeContract;
}

void WorkspaceInternals::rebuild_recovery_members(sqlite3* database)
{
    execute(database, "DELETE FROM deleted_block_members");
    sql::Statement fragments(database, "SELECT id,fragment FROM deleted_blocks");
    while (fragments.row())
        storage_detail::add_recovery_members(database, fragments.text(0), decode_document(fragments.text(1)));
}

Workspace Workspace::open(const std::filesystem::path& directory)
{
    namespace fs = std::filesystem;
    if (directory.empty() || !directory.is_absolute())
        throw std::invalid_argument("Choose an absolute workspace folder");
    const auto root = fs::weakly_canonical(directory);
    if (fs::exists(root) && !fs::is_directory(root))
        throw std::runtime_error("The workspace location is a file, not a folder");
    const auto filename = root / "nebula.sqlite3";
    const bool existed = fs::exists(filename) || fs::is_symlink(filename);
    // A backup or export changes meaning once edited; refuse it before writing.
    if (fs::exists(root / "backup-manifest.json"))
        throw StorageError(ErrorKind::Unsupported,
                           "This folder is a Nebula backup. Restore it into a new folder instead of opening it");
    if (!existed && (fs::exists(root / "workspace.json") || fs::exists(root / "CONVERSION.txt")))
        throw StorageError(ErrorKind::Unsupported,
                           "This folder is a Nebula export. Import it instead of opening it as a workspace");
    if (!fs::exists(root)) {
        fs::create_directories(root.parent_path());
        files::make_private_directory(root);
    }
    const auto attachments = root / "attachments";
    if (fs::is_symlink(attachments))
        throw std::runtime_error("The managed attachments directory must not be a symlink");
    if (fs::exists(attachments) && !fs::is_directory(attachments))
        throw std::runtime_error("The attachments location is a file, not a folder");
    if (fs::is_symlink(filename) || (existed && !fs::is_regular_file(filename)))
        throw std::runtime_error("The database location must be a regular file");
    std::shared_ptr<files::SessionLock> session;
    bool created_file = false;
    if (!existed) {
        files::create_private_file(filename);
        created_file = true;
    }
    sqlite3* raw = nullptr;
    const int opened =
        sqlite3_open_v2(filename.u8string().c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr);
    std::unique_ptr<sqlite3, DatabaseCloser> database(raw);
    auto discard = [&] {
        if (created_file) {
            database.reset();
            std::error_code ignored;
            fs::remove(filename, ignored);
        }
    };
    if (opened != SQLITE_OK) {
        const std::string message = raw ? sqlite3_errmsg(raw) : "allocation failed";
        discard();
        throw StorageError(sql::kind_of(opened), "Cannot open workspace database: " + message);
    }
    if (sqlite3_db_readonly(raw, "main") != 0) {
        discard();
        throw StorageError(ErrorKind::Io, "The workspace database is read-only");
    }
    bool migrated = false;
    std::string id;
    try {
        schema::harden(raw);
        execute(raw, "PRAGMA foreign_keys=ON");
        execute(raw, "PRAGMA synchronous=FULL");
        schema::require_fts5(raw);
        // Identify the file before creating the session lock beside it.
        const bool blank = sql::scalar(raw, "PRAGMA application_id") == "0" &&
                           sql::scalar(raw, "PRAGMA user_version") == "0" &&
                           sql::scalar(raw, "SELECT count(*) FROM sqlite_schema") == "0";
        if (!blank && sql::scalar(raw, "PRAGMA application_id") != std::to_string(schema::ApplicationId))
            throw StorageError(ErrorKind::Unsupported,
                               "This folder contains a database that is not a Nebula workspace");
        session = files::acquire_session(root);
        execute(raw, "BEGIN IMMEDIATE");
        try {
            const auto application = sql::scalar(raw, "PRAGMA application_id");
            auto version = sql::scalar(raw, "PRAGMA user_version");
            const auto objects = sql::scalar(raw, "SELECT count(*) FROM sqlite_schema");
            if (application == "0" && version == "0" && objects == "0") {
                execute(raw, "PRAGMA application_id=" + std::to_string(schema::ApplicationId));
                execute(raw, "CREATE TABLE workspace_meta (id INTEGER PRIMARY KEY CHECK(id=1),"
                             " workspace_id TEXT NOT NULL UNIQUE)");
                execute(raw, "INSERT INTO workspace_meta VALUES (1, lower(hex(randomblob(16))))");
                version = "1";
            } else if (application != std::to_string(schema::ApplicationId)) {
                throw StorageError(ErrorKind::Unsupported,
                                   "This folder contains a database that is not a Nebula workspace");
            } else if (!schema::supported(version)) {
                throw StorageError(ErrorKind::Unsupported,
                                   "This workspace format is not supported by this version of Nebula");
            }
            schema::check_objects(raw);
            id = sql::scalar(raw, "SELECT workspace_id FROM workspace_meta WHERE id=1");
            if (!valid_identity(id))
                throw StorageError(ErrorKind::Corrupt, "The workspace identity is missing");
            for (const auto& step : migrations) {
                if (std::stoi(version) >= step.to)
                    continue;
                step.apply(raw);
                version = std::to_string(step.to);
                execute(raw, "PRAGMA user_version=" + version);
                migrated = true;
            }
            if (std::stoi(version) >= 8 && migrated)
                storage_detail::rebuild_search(raw);
            schema::check_objects(raw, schema::current_objects());
            execute(raw, "SELECT id,database_id,title,revision,trashed FROM pages LIMIT 0");
            if (migrated) {
                const auto problems = integrity_problems(raw);
                if (!problems.empty())
                    throw StorageError(ErrorKind::Corrupt, "Workspace integrity check failed: " + problems.front());
                record_check(raw);
            }
            // Validate the attachment path before committing a newly created database.
            files::make_private_directory(attachments);
            files::initialize_lock(root);
            execute(raw, "COMMIT");
        } catch (...) {
            sqlite3_exec(raw, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
        // Routine checks scan the whole database, so they run under a read
        // transaction and only when the last check is older than a week.
        if (!migrated && check_due(raw)) {
            execute(raw, "BEGIN");
            std::vector<std::string> problems;
            try {
                problems = integrity_problems(raw);
            } catch (...) {
                sqlite3_exec(raw, "ROLLBACK", nullptr, nullptr, nullptr);
                throw;
            }
            execute(raw, "COMMIT");
            if (!problems.empty())
                throw StorageError(ErrorKind::Corrupt, "Workspace integrity check failed: " + problems.front());
            sql::Transaction stamp(raw);
            record_check(raw);
            stamp.commit();
        }
    } catch (...) {
        discard();
        throw;
    }
    Workspace result(root, id, std::move(database));
    result.session_ = std::move(session);
    return result;
}

IntegrityReport Workspace::check_integrity()
{
    IntegrityReport report;
    {
        sql::execute(database_.get(), "BEGIN");
        try {
            report.problems = integrity_problems(database_.get());
        } catch (...) {
            sqlite3_exec(database_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
        sql::execute(database_.get(), "COMMIT");
    }
    try {
        schema::check_objects(database_.get(), schema::current_objects());
    } catch (const std::exception& error) {
        report.problems.push_back(error.what());
    }
    if (report.ok()) {
        sql::Transaction stamp(database_.get());
        record_check(database_.get());
        stamp.commit();
    }
    return report;
}

} // namespace nebula
