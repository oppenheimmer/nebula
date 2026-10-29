#pragma once

#include <filesystem>
#include <memory>
#include <string>

struct sqlite3;

namespace nebula {

struct DatabaseCloser {
    void operator()(sqlite3* database) const noexcept;
};

// Workspace identity/bootstrap only. Page storage and autosave are later work.
class Workspace {
public:
    static Workspace open(const std::filesystem::path& directory);
    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path database_path() const { return directory_ / "nebula.sqlite3"; }
    const std::string& id() const { return id_; }

private:
    Workspace(std::filesystem::path directory, std::string id,
              std::unique_ptr<sqlite3, DatabaseCloser> database);
    std::filesystem::path directory_;
    std::string id_;
    std::unique_ptr<sqlite3, DatabaseCloser> database_;
};

} // namespace nebula
