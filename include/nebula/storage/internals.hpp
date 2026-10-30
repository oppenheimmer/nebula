#pragma once
// Narrow storage access for the interchange module. It exposes the connection
// and the transactional primitives interchange needs, nothing from the desktop.
#include "nebula/storage/sql.hpp"
#include "nebula/storage/workspace.hpp"

namespace nebula {
struct WorkspaceInternals {
    static sqlite3* connection(const Workspace& workspace) { return workspace.database_.get(); }
    static void set_identity(Workspace& workspace, std::string id) { workspace.id_ = std::move(id); }
    static PageRecord create_page_in_transaction(Workspace& workspace, const std::string& database,
                                                 const std::string& title, const std::vector<Block>& blocks,
                                                 const std::string& identity)
    {
        return workspace.create_page_in_transaction(database, title, {}, blocks, identity);
    }
    static std::vector<AttachmentIssue> audit_unlocked(const Workspace& workspace)
    {
        return workspace.audit_attachments_unlocked();
    }
    // Derived-structure maintenance shared with imports.
    static void rebuild_search(sqlite3* database);
    static void rebuild_recovery_members(sqlite3* database);
    static int schema_version(sqlite3* database);
    static int current_schema();
    static int interchange_contract();
    // Chunked SQLite copy into an already-created private file. The returned
    // query-only connection uses the source's immutable managed attachments.
    static Workspace snapshot(const Workspace& source, const std::filesystem::path& file);
};
} // namespace nebula
