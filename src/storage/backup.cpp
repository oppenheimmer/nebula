#include "nebula/storage/workspace.hpp"
#include <sqlite3.h>
#include "sql.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/attachments/manifest.hpp"
#include <stdexcept>

namespace nebula {
Workspace Workspace::read_only(const std::filesystem::path& directory)
{
    namespace fs=std::filesystem;
    if (!directory.is_absolute() || !fs::is_directory(directory)) throw std::invalid_argument("Choose an existing workspace folder");
    const auto root=fs::canonical(directory);
    const auto file=root / "nebula.sqlite3";
    if (!fs::is_regular_file(file) || fs::is_symlink(file)) throw std::runtime_error("Backup database is missing or is not a regular file");
    sqlite3* raw=nullptr;
    const int opened=sqlite3_open_v2(file.u8string().c_str(),&raw,SQLITE_OPEN_READONLY,nullptr);
    std::unique_ptr<sqlite3,DatabaseCloser> db(raw);
    if (opened!=SQLITE_OK) throw std::runtime_error("Cannot read backup database");
    if (sql::scalar(raw,"PRAGMA application_id")!=std::to_string(0x4e454255)) throw std::runtime_error("Not a Nebula workspace");
    const auto version=sql::scalar(raw,"PRAGMA user_version");
    if (version!="1" && version!="2" && version!="3" && version!="4" && version!="5" && version!="6" && version!="7" && version!="8") throw std::runtime_error("Unsupported backup format");
    if (sql::scalar(raw,"PRAGMA quick_check")!="ok") throw std::runtime_error("Backup integrity check failed");
    const auto id=sql::scalar(raw,"SELECT workspace_id FROM workspace_meta WHERE id=1");
    return Workspace(root,id,std::move(db));
}
void Workspace::backup_to(const std::filesystem::path& destination) const
{
    namespace fs = std::filesystem;
    if (!destination.is_absolute()) throw std::invalid_argument("Backup destination must be absolute");
    const auto target = fs::weakly_canonical(destination);
    const bool managed=std::stoi(sql::scalar(database_.get(),"PRAGMA user_version"))>=7;
    std::optional<files::WorkspaceLock> lock;
    if (managed) lock.emplace(directory_,false);
    if (fs::is_symlink(directory_/"attachments") || !fs::is_directory(directory_/"attachments"))
        throw std::runtime_error("The managed attachments directory is missing or is a symlink");
    auto relative = target.lexically_relative(directory_);
    if (relative.empty() || *relative.begin() != "..") throw std::invalid_argument("Choose a backup location outside the workspace");
    if (!fs::create_directory(target)) throw std::runtime_error("Backup destination already exists");
    try {
        sqlite3* raw = nullptr;
        const int opened = sqlite3_open_v2((target / "nebula.sqlite3").u8string().c_str(),&raw,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,nullptr);
        std::unique_ptr<sqlite3,DatabaseCloser> output(raw);
        if (opened != SQLITE_OK) throw std::runtime_error("Cannot create backup database");
        sql::execute(raw,"PRAGMA synchronous=FULL");
        sqlite3_backup* backup = sqlite3_backup_init(raw,"main",database_.get(),"main");
        if (!backup) throw std::runtime_error(sqlite3_errmsg(raw));
        const int step = sqlite3_backup_step(backup,-1);
        const int finish = sqlite3_backup_finish(backup);
        if (step != SQLITE_DONE || finish != SQLITE_OK) throw std::runtime_error("Database backup failed; retry when the workspace is idle");
        output.reset();
        fs::create_directory(target / "attachments");
        std::vector<fs::path> directories{target/"attachments"};
        for (const auto& file : fs::recursive_directory_iterator(directory_ / "attachments")) {
            if (file.is_symlink()) throw std::runtime_error("Attachment symlinks cannot be backed up");
            const auto dest = target / "attachments" / file.path().lexically_relative(directory_ / "attachments");
            if (file.is_directory()) { fs::create_directory(dest); directories.push_back(dest); }
            else if (file.is_regular_file()) files::copy_new(file.path(),dest);
            else throw std::runtime_error("Unsupported attachment file");
        }
        for (auto it=directories.rbegin();it!=directories.rend();++it) files::sync_directory(*it);
        files::initialize_lock(target);
        if (managed) {
            auto snapshot=Workspace::read_only(target);
            for (const auto& issue:snapshot.audit_attachments())
                if (issue.prevents_backup) throw std::runtime_error("Attachment backup is incomplete: "+issue.message+" ["+issue.attachment_id+"]");
        }
        files::write_backup_manifest(target,id_);
    } catch (...) { fs::remove_all(target); throw; }
}
Workspace Workspace::restore(const std::filesystem::path& backup, const std::filesystem::path& destination)
{
    auto source = Workspace::read_only(backup);
    const auto version=std::stoi(sql::scalar(source.database_.get(),"PRAGMA user_version"));
    files::verify_backup_manifest(source.directory_,source.id_,version>=7);
    source.backup_to(destination);
    std::filesystem::remove(destination/"backup-manifest.json"); files::sync_directory(destination);
    auto restored=Workspace::open(destination); restored.rebuild_links();
    if (version>=8) restored.rebuild_search();
    return restored;
}
} // namespace nebula
