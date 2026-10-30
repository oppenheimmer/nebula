#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace nebula::files {
struct Digest { std::int64_t size = 0; std::string sha256; };
// Linux/macOS adapter. Managed files are immutable, regular, and not symlinks.
Digest digest(const std::filesystem::path& path);
Digest copy_new(const std::filesystem::path& source, const std::filesystem::path& destination);
void sync_directory(const std::filesystem::path& directory);
void write_new(const std::filesystem::path& path, const std::string& text);
void initialize_lock(const std::filesystem::path& directory);
class WorkspaceLock {
public:
    WorkspaceLock(const std::filesystem::path& directory, bool exclusive);
    ~WorkspaceLock();
    WorkspaceLock(const WorkspaceLock&) = delete;
    WorkspaceLock& operator=(const WorkspaceLock&) = delete;
private:
    int descriptor_ = -1;
};
} // namespace nebula::files
