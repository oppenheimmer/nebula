#pragma once
#include "nebula/attachments/files.hpp"
#include "nebula/qt/utf8.hpp"
#include "nebula/storage/internals.hpp"
#include "nebula/storage/sql.hpp"
#include <QFile>
#include <QString>
#include <algorithm>

namespace nebula::interchange::detail {
namespace fs = std::filesystem;
inline QString q(const std::string& value)
{
    return utf8::checked<std::runtime_error>(value, "Interchange requires valid UTF-8");
}
inline std::string s(const QString& value)
{
    return utf8::encode(value);
}
inline std::string read(const fs::path& path)
{
    const auto digest = files::digest(path);
    if (digest.size > 64 * 1024 * 1024)
        throw std::runtime_error("Interchange text files are limited to 64 MiB each");
    QFile file(q(path.u8string()));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot read interchange file");
    const auto bytes = file.readAll();
    if (file.error() != QFile::NoError || bytes.size() != digest.size)
        throw std::runtime_error("Interchange file changed while reading");
    return std::string(bytes.constData(), std::size_t(bytes.size()));
}
inline void warn(InterchangeReport& report, const std::string& message)
{
    if (std::find(report.warnings.begin(), report.warnings.end(), message) == report.warnings.end())
        report.warnings.push_back(message);
}
class Snapshot {
    sqlite3* db_;

public:
    explicit Snapshot(sqlite3* db) : db_(db) { sql::execute(db_, "BEGIN"); }
    ~Snapshot() { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
};
class NewDirectory {
    fs::path path_;
    bool complete_ = false;

public:
    explicit NewDirectory(const fs::path& path) : path_(path)
    {
        if (!path.is_absolute() || fs::is_symlink(path) || fs::exists(path) || !files::make_private_directory(path))
            throw std::runtime_error("Choose a new absolute destination folder");
    }
    ~NewDirectory()
    {
        if (!complete_) {
            std::error_code error;
            fs::remove_all(path_, error);
        }
    }
    void complete()
    {
        files::sync_directory(path_);
        files::sync_directory(path_.parent_path());
        complete_ = true;
    }
};
inline void reject_managed_destination(const fs::path& workspace, const fs::path& destination)
{
    const auto relative = fs::weakly_canonical(destination).lexically_relative(workspace / "attachments");
    if (relative.empty() || *relative.begin() != "..")
        throw std::invalid_argument("Choose a destination outside managed attachments");
}
} // namespace nebula::interchange::detail
