#pragma once

#include "nebula/attachments/files.hpp"
#include "nebula/core/database.hpp"
#include "nebula/storage/errors.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>

struct sqlite3;

namespace nebula {

struct DatabaseCloser {
    void operator()(sqlite3* database) const noexcept;
};

struct LinkEdge {
    std::string source_page, source_block, source_title, target_id;
    ReferenceKind kind;
    std::string source_context, label, unresolved_name, database_scope;
    std::int64_t position = 0, run_index = 0;
};
struct DeletedBlockGroup {
    std::string id, page_id, page_title, label;
};
enum class ReferenceState { Available, Unresolved, PageTrashed, DatabaseTrashed, Unavailable };
struct ReferenceTarget {
    ReferenceState state = ReferenceState::Unavailable;
    std::string page_id, block_id;
};
// In-memory workspace operation, separate from per-page text undo. Revision
// checks protect both documents when undoing/redoing after navigation.
struct BlockTransfer {
    Page source_before, destination_before, source_after, destination_after;
    std::int64_t source_revision = 0, destination_revision = 0;
    bool applied = true;
};
struct AttachmentInfo {
    std::string id, name, sha256;
    std::int64_t size = 0;
    bool image = false;
};
struct ImportedAttachment {
    AttachmentInfo file;
    std::string card, following;
};
struct AttachmentIssue {
    std::string path, attachment_id, message;
    bool removable = false, prevents_backup = false;
};
struct SearchResult {
    std::string page_id, block_id, title, database_name, context;
};
struct InterchangeReport {
    std::vector<std::string> pages, warnings;
};
// Lightweight rows for pickers and lists; no block trees are decoded.
struct PageSummary {
    std::string id, title, database_id, database_name;
    bool trashed = false, database_trashed = false;
};
struct LinkTargetRow {
    ReferenceKind kind = ReferenceKind::Page;
    std::string id, label, page_id, page_title, database_name;
    bool heading = false;
};
struct PurgeReport {
    std::size_t databases = 0, pages = 0, fragments = 0, files = 0;
    bool compacted = false;
};
struct IntegrityReport {
    std::vector<std::string> problems;
    bool ok() const { return problems.empty(); }
};

class Workspace;
// A managed file being copied. prepare/commit run on the database thread;
// copy() touches only files and may run on a worker thread.
class StagedAttachment {
public:
    StagedAttachment(StagedAttachment&& other) noexcept;
    StagedAttachment& operator=(StagedAttachment&&) = delete;
    ~StagedAttachment();
    void copy(const files::Progress& progress = {});
    const std::string& identity() const { return identity_; }

private:
    friend class Workspace;
    StagedAttachment() = default;
    std::filesystem::path source_, stage_, destination_;
    std::string identity_, name_;
    files::Digest digest_;
    std::unique_ptr<files::WorkspaceLock> lock_;
    bool image_ = false, promoted_ = false, committed_ = false;
};

class Workspace {
public:
    // Opening a writable workspace holds a session lock; another process
    // opening the same folder is refused. Backup/export folders are refused.
    static Workspace open(const std::filesystem::path& directory);
    static Workspace read_only(const std::filesystem::path& directory);
    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path database_path() const { return directory_ / "nebula.sqlite3"; }
    const std::string& id() const { return id_; }
    // Runs several operations as one atomic commit; nested operations become savepoints.
    void transaction(const std::function<void()>& work);
    Database create_database(const std::string& name);
    // Journal database with Entry date, Mood and Tags plus its default view, atomically.
    Database create_journal(const std::string& name = "Journal");
    std::vector<Database> databases(bool include_trashed = false) const;
    void rename_database(const std::string& id, const std::string& name);
    void trash_database(const std::string& id, bool trashed);
    PropertyDefinition add_property(const std::string& database, const std::string& name, PropertyKind kind);
    void rename_property(const std::string& id, const std::string& name);
    // Removes the definition, its stored values and view settings that use it.
    void delete_property(const std::string& id);
    std::vector<PropertyDefinition> properties(const std::string& database) const;
    PageRecord create_page(const std::string& database, const std::string& title,
                           const std::map<std::string, PropertyValue>& properties = {},
                           const std::vector<Block>& initial_blocks = {});
    // Adds a missing Entry date property and the dated page in one transaction.
    PageRecord create_dated_entry(const std::string& database, const std::string& date);
    PageRecord load_page(const std::string& id) const;
    std::optional<std::string> page_title(const std::string& id) const;
    std::vector<PageRecord> pages(const std::string& database, bool include_trashed = false) const;
    std::vector<PageSummary> page_summaries(bool include_trashed = false) const;
    std::vector<LinkTargetRow> link_targets() const;
    // Optimistic revision check; updates revision only after commit. Blocks
    // listed as absorbed were merged into another block and are not archived.
    void save_page(PageRecord& page, const std::unordered_set<std::string>& absorbed = {});
    // Move committed records. Flush pending edits before either workspace move.
    void move_page(PageRecord& page, const std::string& database);
    BlockTransfer transfer_blocks(const PageRecord& source, const PageRecord& destination, const std::string& block);
    void set_transfer_applied(BlockTransfer& transfer, bool applied);
    ReferenceTarget resolve_reference(const Reference& reference) const;
    std::optional<std::string> reference_label(const Reference& reference) const;
    PageRecord create_reference_target(PageRecord& source, const ReferenceLocation& location,
                                       const std::string& database, const std::string& title);
    StagedAttachment prepare_attachment(const std::filesystem::path& source, bool image);
    ImportedAttachment commit_attachment(PageRecord& page, const std::string& after, StagedAttachment& staged);
    ImportedAttachment import_attachment(PageRecord& page, const std::string& after,
                                         const std::filesystem::path& source, bool image);
    std::optional<AttachmentInfo> attachment(const std::string& id) const;
    std::vector<AttachmentInfo> attachments() const;
    std::filesystem::path attachment_path(const std::string& id) const;
    std::vector<AttachmentIssue> audit_attachments() const;
    void remove_unused_attachment(const std::string& relative_path);
    void trash_page(const std::string& id, bool trashed);
    std::vector<DeletedBlockGroup> deleted_blocks() const;
    // Restores missing identities. If the original parent now lives on another
    // page, the fragment returns as top-level blocks and reparented is set.
    PageRecord restore_deleted_blocks(const std::string& id, bool* reparented = nullptr);
    // Permanently deletes trashed databases/pages, every deleted-block fragment and
    // registered files no remaining page references, then compacts the database.
    // Callers must first flush and clear editor undo that could restore them.
    PurgeReport empty_trash();
    void save_view(View& view);
    void delete_view(const std::string& id);
    std::vector<View> views(const std::string& database) const;
    // Rows carry titles, revisions and properties; their block trees are empty.
    // Text sorting and Contains are case-insensitive and locale-aware.
    std::vector<PageRecord> query_view(const View& view) const;
    std::vector<LinkEdge> backlinks(ReferenceKind kind, const std::string& id) const;
    std::vector<LinkEdge> outgoing_links(const std::string& page) const;
    void rebuild_links();
    std::vector<SearchResult> search(const std::string& text, const std::string& database = {},
                                     const std::vector<LinkFilter>& filters = {}) const;
    void rebuild_search();
    // Full SQLite and relationship check. Opening runs it after migrations and
    // when the previous check is older than a week.
    IntegrityReport check_integrity();
    void backup_to(const std::filesystem::path& destination) const;
    static Workspace restore(const std::filesystem::path& backup, const std::filesystem::path& destination);

private:
    friend struct WorkspaceInternals;
    Workspace(std::filesystem::path directory, std::string id, std::unique_ptr<sqlite3, DatabaseCloser> database);
    std::filesystem::path directory_;
    std::string id_;
    std::unique_ptr<sqlite3, DatabaseCloser> database_;
    std::shared_ptr<files::SessionLock> session_;
    void replace_transfer_documents(const Page& source, std::int64_t source_revision, const Page& destination,
                                    std::int64_t destination_revision);
    PageRecord create_page_in_transaction(const std::string& database, const std::string& title,
                                          const std::map<std::string, PropertyValue>& properties,
                                          const std::vector<Block>& blocks, const std::string& identity = {});
    void save_page_in_transaction(PageRecord& page, const std::unordered_set<std::string>& absorbed = {});
    std::vector<AttachmentIssue> audit_attachments_unlocked() const;
};

} // namespace nebula
