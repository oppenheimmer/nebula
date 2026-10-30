#pragma once
#include <filesystem>
#include <string>

namespace nebula::files {
void write_backup_manifest(const std::filesystem::path& root, const std::string& workspace);
void verify_backup_manifest(const std::filesystem::path& root, const std::string& workspace, bool required);
} // namespace nebula::files
