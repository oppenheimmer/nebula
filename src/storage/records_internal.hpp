#pragma once
#include "sql.hpp"
#include "nebula/core/database.hpp"
struct sqlite3;
namespace nebula::storage_detail {
PageRecord load_page(sqlite3* database, const std::string& id);
std::optional<std::string> reference_label(sqlite3* database, const Reference& reference);
void create_search(sqlite3* database);
void rebuild_search(sqlite3* database);
void refresh_search(sqlite3* database, const std::string& page, const std::vector<std::string>& removed_blocks = {});
std::string link_predicate(const std::vector<LinkFilter>& filters);
void bind_link_filters(sql::Statement& query,int first,const std::vector<LinkFilter>& filters);
} // namespace nebula::storage_detail
