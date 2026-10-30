#pragma once
#include "nebula/core/database.hpp"
struct sqlite3;
namespace nebula::storage_detail {
PageRecord load_page(sqlite3* database, const std::string& id);
std::optional<std::string> reference_label(sqlite3* database, const Reference& reference);
void create_search(sqlite3* database);
void rebuild_search(sqlite3* database);
void refresh_search(sqlite3* database, const std::string& page, const std::vector<std::string>& removed_blocks = {});
} // namespace nebula::storage_detail
