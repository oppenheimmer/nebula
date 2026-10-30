#pragma once
#include "nebula/attachments/files.hpp"
#include <filesystem>
#include <map>
#include <string>

namespace nebula::files {
// Relative path ("nebula.sqlite3", "attachments/<id>") to the digest computed while copying.
using Digests = std::map<std::string, Digest>;
void write_backup_manifest(const std::filesystem::path& root, const std::string& workspace, const Digests& digests);
// Verifies every listed file once and returns the verified digests.
Digests verify_backup_manifest(const std::filesystem::path& root, const std::string& workspace, bool required);
} // namespace nebula::files
