#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include "nebula/core/database.hpp"

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
struct DeletedBlockGroup { std::string id, page_id, page_title, label; };
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

class Workspace {
public:
    static Workspace open(const std::filesystem::path& directory);
    static Workspace read_only(const std::filesystem::path& directory);
    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path database_path() const { return directory_ / "nebula.sqlite3"; }
    const std::string& id() const { return id_; }
    Database create_database(const std::string& name);
    std::vector<Database> databases(bool include_trashed = false) const;
    void rename_database(const std::string& id, const std::string& name);
    void trash_database(const std::string& id, bool trashed);
    PropertyDefinition add_property(const std::string& database, const std::string& name, PropertyKind kind);
    std::vector<PropertyDefinition> properties(const std::string& database) const;
    PageRecord create_page(const std::string& database, const std::string& title,
                          const std::map<std::string,PropertyValue>& properties = {},
                          const std::vector<Block>& initial_blocks = {});
    PageRecord load_page(const std::string& id) const;
    std::vector<PageRecord> pages(const std::string& database, bool include_trashed = false) const;
    void save_page(PageRecord& page); // Optimistic revision check; updates revision only after commit.
    // Move committed records. Flush pending edits before either workspace move.
    void move_page(PageRecord& page, const std::string& database);
    BlockTransfer transfer_blocks(const PageRecord& source, const PageRecord& destination,
                                  const std::string& block);
    void set_transfer_applied(BlockTransfer& transfer, bool applied);
    ReferenceTarget resolve_reference(const Reference& reference) const;
    void trash_page(const std::string& id, bool trashed);
    std::vector<DeletedBlockGroup> deleted_blocks() const;
    PageRecord restore_deleted_blocks(const std::string& id);
    void save_view(View& view);
    std::vector<View> views(const std::string& database) const;
    std::vector<LinkEdge> backlinks(ReferenceKind kind, const std::string& id) const;
    std::vector<LinkEdge> outgoing_links(const std::string& page) const;
    void rebuild_links();
    void backup_to(const std::filesystem::path& destination) const;
    static Workspace restore(const std::filesystem::path& backup, const std::filesystem::path& destination);

private:
    Workspace(std::filesystem::path directory, std::string id,
              std::unique_ptr<sqlite3, DatabaseCloser> database);
    std::filesystem::path directory_;
    std::string id_;
    std::unique_ptr<sqlite3, DatabaseCloser> database_;
    void replace_transfer_documents(const Page& source, std::int64_t source_revision,
                                    const Page& destination, std::int64_t destination_revision);
};

} // namespace nebula
