#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace nebula::files {
struct Digest { std::int64_t size = 0; std::string sha256; };
// Reports copied bytes; return false to cancel. Called from the copying thread.
using Progress = std::function<bool(std::int64_t copied, std::int64_t total)>;
// Linux/macOS adapter. Managed files are immutable, regular, and not symlinks.
Digest digest(const std::filesystem::path& path);
Digest copy_new(const std::filesystem::path& source, const std::filesystem::path& destination,
                const Progress& progress = {});
// Open root/relative one component at a time with O_NOFOLLOW, so a symlink
// swapped into any component after a directory check cannot redirect access.
Digest digest_beneath(const std::filesystem::path& root, const std::filesystem::path& relative);
Digest copy_beneath(const std::filesystem::path& root, const std::filesystem::path& relative,
                    const std::filesystem::path& destination);
void sync_directory(const std::filesystem::path& directory);
void write_new(const std::filesystem::path& path, const std::string& text);
// Nebula-owned folders are 0700 and databases 0600. Returns false if it existed.
bool make_private_directory(const std::filesystem::path& directory);
void create_private_file(const std::filesystem::path& path);
void make_read_only(const std::filesystem::path& path);
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
// Held for the lifetime of a writable Workspace. Another process opening the
// same folder fails immediately; objects in one process share the lock.
class SessionLock;
std::shared_ptr<SessionLock> acquire_session(const std::filesystem::path& directory);
} // namespace nebula::files
