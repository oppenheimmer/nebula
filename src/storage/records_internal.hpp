#pragma once
#include "nebula/core/database.hpp"
#include "nebula/storage/sql.hpp"
#include <map>
#include <optional>
struct sqlite3;
namespace nebula::storage_detail {
PageRecord load_page(sqlite3* database, const std::string& id);
// Title, revision, trash flag and properties without decoding the block tree.
PageRecord load_summary(sqlite3* database, const std::string& id);
std::optional<std::string> reference_label(sqlite3* database, const Reference& reference);
// Search rows: one page row (title and properties) plus one row per nonempty
// block body. page_search_rows maps them to FTS rowids for indexed deletes.
void create_search(sqlite3* database);
void rebuild_search(sqlite3* database);
void index_page(sqlite3* database, const PageRecord& page);
void remove_page_search(sqlite3* database, const std::string& page);
struct SearchChange {
    std::vector<std::string> blocks, removed; // changed or new block rows; deleted rows
    bool page_row = false;                    // title or properties changed
    bool label_title = false;                 // page title shown by default labels changed
    std::vector<std::string> label_blocks;    // block titles shown by default labels changed/removed
};
void refresh_search(sqlite3* database, const PageRecord& page, const SearchChange& change);
// Reindex pages whose default labels display the given page title or blocks.
void reindex_referrers(sqlite3* database, const std::string& page, bool title, const std::vector<std::string>& blocks);
std::string link_predicate(const std::vector<LinkFilter>& filters);
void bind_link_filters(sql::Statement& query, int first, const std::vector<LinkFilter>& filters);
// Locale-aware, case-insensitive ordering and Contains for view results.
TextRules text_rules();
void insert_links(sqlite3* database, const std::string& page, const Block& block);
void add_recovery_members(sqlite3* database, const std::string& fragment, const Page& blocks);
} // namespace nebula::storage_detail
