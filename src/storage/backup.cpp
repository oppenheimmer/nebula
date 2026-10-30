#include "nebula/storage/workspace.hpp"
#include <sqlite3.h>
#include "nebula/attachments/files.hpp"
#include "nebula/attachments/manifest.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/storage/internals.hpp"
#include "nebula/storage/sql.hpp"
#include "schema.hpp"
#include <stdexcept>
#include <unordered_set>

namespace nebula {
namespace {
namespace fs=std::filesystem;
// Step in chunks so a GUI autosave can commit between steps. A source that keeps
// changing restarts the copy; after a few restarts one final step holds the lock.
void copy_database(sqlite3* source,sqlite3* output) {
    sqlite3_backup* backup=sqlite3_backup_init(output,"main",source,"main");
    if (!backup) throw StorageError(ErrorKind::Io,sqlite3_errmsg(output));
    int restarts=0,previous=-1,busy=0,status=SQLITE_OK;
    while (true) {
        status=sqlite3_backup_step(backup,restarts>=3?-1:256);
        if (status==SQLITE_DONE) break;
        if (status!=SQLITE_OK && status!=SQLITE_BUSY && status!=SQLITE_LOCKED) break;
        const int remaining=sqlite3_backup_remaining(backup);
        if (previous>=0 && remaining>previous) ++restarts;
        previous=remaining;
        if (status!=SQLITE_OK) { if (++busy>600) break; sqlite3_sleep(5); } else busy=0;
    }
    const int finish=sqlite3_backup_finish(backup);
    if (status!=SQLITE_DONE || finish!=SQLITE_OK)
        throw StorageError(sql::kind_of(status==SQLITE_DONE?finish:status),"Database backup failed; retry when the workspace is idle");
}
void collect(const Page& page,std::unordered_set<std::string>& ids) {
    for (const auto& block:page.blocks) {
        if (!block.attachment_id.empty()) ids.insert(block.attachment_id);
        for (const auto& run:block.runs) if (run.asset && !run.asset->attachment_id.empty()) ids.insert(run.asset->attachment_id);
    }
}
// Registered metadata must match the bytes hashed while copying; referenced IDs
// must be registered. This replaces a second full read of every file.
void verify_snapshot(const fs::path& target,const files::Digests& digests) {
    auto snapshot=Workspace::read_only(target);
    sqlite3* db=WorkspaceInternals::connection(snapshot);
    std::unordered_set<std::string> registered,referenced;
    for (const auto& file:snapshot.attachments()) {
        registered.insert(file.id);
        const auto found=digests.find("attachments/"+file.id);
        if (found==digests.end()) throw std::runtime_error("Attachment backup is incomplete: Managed file unavailable ["+file.id+"]");
        if (found->second.size!=file.size || found->second.sha256!=file.sha256)
            throw std::runtime_error("Attachment backup is incomplete: Managed file checksum differs from its imported bytes ["+file.id+"]");
    }
    { sql::Statement pages(db,"SELECT id FROM pages"); while (pages.row()) collect(snapshot.load_page(pages.text(0)).document,referenced); }
    { sql::Statement fragments(db,"SELECT fragment FROM deleted_blocks"); while (fragments.row()) collect(decode_document(fragments.text(0)),referenced); }
    for (const auto& id:referenced)
        if (!registered.count(id)) throw std::runtime_error("Attachment backup is incomplete: Referenced attachment metadata is unavailable ["+id+"]");
}
// Copies a consistent database snapshot and every attachment file. When expected
// digests are given (restore), each copied file must match them.
files::Digests copy_workspace(const Workspace& workspace,const fs::path& target,const files::Digests* expected) {
    const auto& directory=workspace.directory();
    files::Digests digests;
    {
        files::create_private_file(target/"nebula.sqlite3");
        sqlite3* raw=nullptr;
        const int opened=sqlite3_open_v2((target/"nebula.sqlite3").u8string().c_str(),&raw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOFOLLOW,nullptr);
        std::unique_ptr<sqlite3,DatabaseCloser> output(raw);
        if (opened!=SQLITE_OK) throw StorageError(sql::kind_of(opened),"Cannot create backup database");
        sql::execute(raw,"PRAGMA synchronous=FULL");
        copy_database(WorkspaceInternals::connection(workspace),raw);
    }
    files::make_private_directory(target/"attachments");
    std::vector<fs::path> directories{target/"attachments"};
    for (const auto& file:fs::recursive_directory_iterator(directory/"attachments")) {
        if (file.is_symlink()) throw std::runtime_error("Attachment symlinks cannot be backed up");
        const auto relative=file.path().lexically_relative(directory);
        const auto dest=target/relative;
        if (file.is_directory()) { files::make_private_directory(dest); directories.push_back(dest); }
        else if (file.is_regular_file()) {
            const auto digest=files::copy_beneath(directory,relative,dest);
            const auto name=relative.generic_u8string();
            if (expected) {
                const auto found=expected->find(name);
                if (found==expected->end() || found->second.size!=digest.size || found->second.sha256!=digest.sha256)
                    throw std::runtime_error("Backup file verification failed: "+name);
            }
            digests.emplace(name,digest);
        } else throw std::runtime_error("Unsupported attachment file");
    }
    for (auto it=directories.rbegin();it!=directories.rend();++it) files::sync_directory(*it);
    files::initialize_lock(target);
    return digests;
}
fs::path new_destination(const fs::path& directory,const fs::path& destination) {
    if (!destination.is_absolute()) throw std::invalid_argument("Backup destination must be absolute");
    const auto target=fs::weakly_canonical(destination);
    auto relative=target.lexically_relative(directory);
    if (relative.empty() || *relative.begin()!="..") throw std::invalid_argument("Choose a backup location outside the workspace");
    if (!files::make_private_directory(target)) throw std::runtime_error("Backup destination already exists");
    return target;
}
} // namespace
Workspace Workspace::read_only(const std::filesystem::path& directory)
{
    if (!directory.is_absolute() || !fs::is_directory(directory)) throw std::invalid_argument("Choose an existing workspace folder");
    const auto root=fs::canonical(directory);
    const auto file=root / "nebula.sqlite3";
    if (!fs::is_regular_file(file) || fs::is_symlink(file)) throw std::runtime_error("Backup database is missing or is not a regular file");
    sqlite3* raw=nullptr;
    const int opened=sqlite3_open_v2(file.u8string().c_str(),&raw,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOFOLLOW,nullptr);
    std::unique_ptr<sqlite3,DatabaseCloser> db(raw);
    if (opened!=SQLITE_OK) throw StorageError(sql::kind_of(opened),"Cannot read backup database");
    schema::harden(raw);
    if (sql::scalar(raw,"PRAGMA application_id")!=std::to_string(schema::ApplicationId)) throw StorageError(ErrorKind::Unsupported,"Not a Nebula workspace");
    const auto version=sql::scalar(raw,"PRAGMA user_version");
    if (!schema::supported(version)) throw StorageError(ErrorKind::Unsupported,"Unsupported backup format");
    schema::check_objects(raw);
    if (sql::scalar(raw,"PRAGMA quick_check")!="ok") throw StorageError(ErrorKind::Corrupt,"Backup integrity check failed");
    const auto id=sql::scalar(raw,"SELECT workspace_id FROM workspace_meta WHERE id=1");
    return Workspace(root,id,std::move(db));
}
void Workspace::backup_to(const std::filesystem::path& destination) const
{
    const bool managed=WorkspaceInternals::schema_version(database_.get())>=7;
    std::optional<files::WorkspaceLock> lock;
    if (managed) lock.emplace(directory_,false);
    if (fs::is_symlink(directory_/"attachments") || !fs::is_directory(directory_/"attachments"))
        throw std::runtime_error("The managed attachments directory is missing or is a symlink");
    const auto target=new_destination(directory_,destination);
    try {
        auto digests=copy_workspace(*this,target,nullptr);
        if (managed) verify_snapshot(target,digests);
        digests.emplace("nebula.sqlite3",files::digest(target/"nebula.sqlite3"));
        files::write_backup_manifest(target,id_,digests);
    } catch (...) { std::error_code ignored; fs::remove_all(target,ignored); throw; }
}
Workspace Workspace::restore(const std::filesystem::path& backup, const std::filesystem::path& destination)
{
    auto source = Workspace::read_only(backup);
    const auto version=WorkspaceInternals::schema_version(source.database_.get());
    const auto verified=files::verify_backup_manifest(source.directory_,source.id_,version>=7);
    std::optional<files::WorkspaceLock> lock;
    if (version>=7) lock.emplace(source.directory_,false);
    if (fs::is_symlink(source.directory_/"attachments") || !fs::is_directory(source.directory_/"attachments"))
        throw std::runtime_error("The backup attachments directory is missing or is a symlink");
    const auto target=new_destination(source.directory_,destination);
    try {
        // Files listed in the manifest were hashed once above; copies must match them.
        copy_workspace(source,target,version>=7?&verified:nullptr);
        files::sync_directory(target);
    } catch (...) { std::error_code ignored; fs::remove_all(target,ignored); throw; }
    lock.reset();
    auto restored=Workspace::open(target); restored.rebuild_links();
    if (version>=8) restored.rebuild_search();
    return restored;
}
} // namespace nebula
