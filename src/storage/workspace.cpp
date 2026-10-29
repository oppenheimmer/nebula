#include "nebula/storage/workspace.hpp"

#include <sqlite3.h>
#include <stdexcept>
#include <utility>

namespace nebula {
namespace {

constexpr int ApplicationId = 0x4e454255; // NEBU
constexpr int SchemaVersion = 1; // Workspace identity only; no page schema yet.

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
    execute(raw, "BEGIN IMMEDIATE");
    try {
        const auto application = scalar(raw, "PRAGMA application_id");
        const auto version = scalar(raw, "PRAGMA user_version");
        const auto objects = scalar(raw, "SELECT count(*) FROM sqlite_schema");
        if (application == "0" && version == "0" && objects == "0") {
            execute(raw, "PRAGMA application_id=" + std::to_string(ApplicationId));
            execute(raw, "PRAGMA user_version=" + std::to_string(SchemaVersion));
            execute(raw, "CREATE TABLE workspace_meta (id INTEGER PRIMARY KEY CHECK(id=1),"
                         " workspace_id TEXT NOT NULL UNIQUE)");
            execute(raw, "INSERT INTO workspace_meta VALUES (1, lower(hex(randomblob(16))))");
        } else if (application != std::to_string(ApplicationId)) {
            throw std::runtime_error("This folder contains a database that is not a Nebula workspace");
        } else if (version != std::to_string(SchemaVersion)) {
            throw std::runtime_error("This workspace format is not supported by this version of Nebula");
        }
        const auto id = scalar(raw, "SELECT workspace_id FROM workspace_meta WHERE id=1");
        if (id.empty())
            throw std::runtime_error("The workspace identity is missing");
        // Validate the attachment path before committing a newly created database.
        fs::create_directories(attachments);
        execute(raw, "COMMIT");
        return Workspace(root, id, std::move(database));
    } catch (...) {
        sqlite3_exec(raw, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

} // namespace nebula
