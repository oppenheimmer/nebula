#include "nebula/storage/workspace.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/storage/sql.hpp"
#include "records_internal.hpp"
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {
bool safe_id(const std::string& id) {
    return id.size()==32 && std::all_of(id.begin(),id.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
void check_directory(const std::filesystem::path& root) {
    if (std::filesystem::is_symlink(root) || !std::filesystem::is_directory(root))
        throw std::runtime_error("The managed attachments directory is missing or unsafe");
}
void collect(const Page& page,std::unordered_set<std::string>& ids) {
    for (const auto& block:page.blocks) {
        if (!block.attachment_id.empty()) ids.insert(block.attachment_id);
        for (const auto& run:block.runs) if (run.asset && !run.asset->attachment_id.empty()) ids.insert(run.asset->attachment_id);
    }
}
} // namespace
StagedAttachment::StagedAttachment(StagedAttachment&& other) noexcept
    : source_(std::move(other.source_)), stage_(std::move(other.stage_)), destination_(std::move(other.destination_)),
      identity_(std::move(other.identity_)), name_(std::move(other.name_)), digest_(std::move(other.digest_)),
      lock_(std::move(other.lock_)), image_(other.image_), promoted_(other.promoted_), committed_(other.committed_)
{
    other.stage_.clear(); other.promoted_=false;
}
StagedAttachment::~StagedAttachment()
{
    std::error_code ignored;
    if (!stage_.empty()) std::filesystem::remove(stage_,ignored);
    if (promoted_ && !committed_) std::filesystem::remove(destination_,ignored);
}
void StagedAttachment::copy(const files::Progress& progress)
{
    if (promoted_ || committed_) throw std::logic_error("This attachment was already copied");
    digest_=files::copy_new(source_,stage_,progress);
    // Link creates the final name without overwriting an existing identity.
    std::filesystem::create_hard_link(stage_,destination_); promoted_=true;
    std::filesystem::remove(stage_); stage_.clear(); files::sync_directory(destination_.parent_path());
}
std::vector<AttachmentInfo> Workspace::attachments() const
{
    sql::Statement query(database_.get(),"SELECT id,name,sha256,size,image FROM attachments ORDER BY id");
    std::vector<AttachmentInfo> result;
    while (query.row()) {
        if (!safe_id(query.text(0))) throw std::runtime_error("Invalid managed attachment identity");
        result.push_back({query.text(0),query.text(1),query.text(2),query.number(3),bool(query.number(4))});
    }
    return result;
}
std::optional<AttachmentInfo> Workspace::attachment(const std::string& id) const
{
    if (!safe_id(id)) return {};
    sql::Statement query(database_.get(),"SELECT name,sha256,size,image FROM attachments WHERE id=?"); query.bind(1,id);
    if (!query.row()) return {};
    return AttachmentInfo{id,query.text(0),query.text(1),query.number(2),bool(query.number(3))};
}
std::filesystem::path Workspace::attachment_path(const std::string& id) const
{
    if (!attachment(id)) throw StorageError(ErrorKind::Unavailable,"Attachment metadata is unavailable: "+id);
    const auto directory=directory_/"attachments"; check_directory(directory); const auto path=directory/id;
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw StorageError(ErrorKind::Unavailable,"Managed attachment file is missing or unsafe: "+id);
    return path;
}
StagedAttachment Workspace::prepare_attachment(const std::filesystem::path& source, bool image)
{
    const auto directory=directory_/"attachments"; check_directory(directory);
    StagedAttachment staged;
    staged.lock_=std::make_unique<files::WorkspaceLock>(directory_,true);
    staged.identity_=sql::id(database_.get()); staged.source_=source; staged.image_=image;
    staged.name_=source.filename().u8string();
    if (staged.name_.empty()) throw std::invalid_argument("Choose a named file");
    staged.stage_=directory/(".import-"+staged.identity_); staged.destination_=directory/staged.identity_;
    return staged;
}
ImportedAttachment Workspace::commit_attachment(PageRecord& page, const std::string& after, StagedAttachment& staged)
{
    if (!staged.promoted_ || staged.committed_) throw std::logic_error("Copy the staged attachment before committing it");
    sql::Transaction transaction(database_.get());
    if (resolve_reference({ReferenceKind::Page,page.document.id}).state!=ReferenceState::Available)
        throw std::runtime_error("Restore the page and its database before importing files");
    const AttachmentInfo file{staged.identity_,staged.name_,staged.digest_.sha256,staged.digest_.size,staged.image_};
    sql::Statement add(database_.get(),"INSERT INTO attachments(id,name,sha256,size,image) VALUES(?,?,?,?,?)");
    add.bind(1,file.id).bind(2,file.name).bind(3,file.sha256).bind(4,file.size).bind(5,file.image).done();
    auto next=page; const auto card=sql::id(database_.get()), following=sql::id(database_.get());
    insert_attachment(next.document,after,file.id,file.name,file.image,card,following);
    save_page_in_transaction(next); transaction.commit();
    staged.committed_=true; staged.lock_.reset(); ++next.revision; page=std::move(next);
    return {file,card,following};
}
ImportedAttachment Workspace::import_attachment(PageRecord& page, const std::string& after,
    const std::filesystem::path& source, bool image)
{
    if (resolve_reference({ReferenceKind::Page,page.document.id}).state!=ReferenceState::Available)
        throw std::runtime_error("Restore the page and its database before importing files");
    auto staged=prepare_attachment(source,image); staged.copy();
    return commit_attachment(page,after,staged);
}
std::vector<AttachmentIssue> Workspace::audit_attachments_unlocked() const
{
    const auto directory=directory_/"attachments"; check_directory(directory);
    std::unordered_set<std::string> referenced;
    sql::Statement pages(database_.get(),"SELECT id FROM pages");
    while (pages.row()) collect(load_page(pages.text(0)).document,referenced);
    // Retain all recovery fragments, including fragments already restored. They
    // remain user-owned recovery records until Empty trash discards them.
    sql::Statement fragments(database_.get(),"SELECT fragment FROM deleted_blocks");
    while (fragments.row()) collect(decode_document(fragments.text(0)),referenced);
    std::unordered_map<std::string,AttachmentInfo> known; std::vector<AttachmentIssue> issues;
    for (const auto& file:attachments()) {
        known.emplace(file.id,file);
        if (!referenced.count(file.id)) issues.push_back({file.id,file.id,"Unreferenced imported file retained for undo and recovery",false,false});
        try {
            const auto actual=files::digest_beneath(directory,file.id);
            if (actual.size!=file.size || actual.sha256!=file.sha256) issues.push_back({file.id,file.id,"Managed file checksum differs from its imported bytes",false,true});
        } catch (const std::exception& e) { issues.push_back({file.id,file.id,std::string("Managed file unavailable: ")+e.what(),false,true}); }
    }
    for (const auto& id:referenced) if (!known.count(id)) issues.push_back({{},id,"Referenced attachment metadata is unavailable",false,true});
    for (const auto& entry:std::filesystem::recursive_directory_iterator(directory)) {
        const auto relative=entry.path().lexically_relative(directory).generic_u8string();
        if (entry.is_symlink() || (!entry.is_directory() && !entry.is_regular_file())) issues.push_back({relative,{},"Unsafe attachment entry; cleanup refused",false,true});
        else if (entry.is_regular_file() && !known.count(relative))
            issues.push_back({relative,{},referenced.count(relative)?"Referenced file has no metadata; retained":"Unregistered file or interrupted import",!referenced.count(relative),bool(referenced.count(relative))});
    }
    return issues;
}
std::vector<AttachmentIssue> Workspace::audit_attachments() const
{
    files::WorkspaceLock lock(directory_,false); return audit_attachments_unlocked();
}
void Workspace::remove_unused_attachment(const std::string& relative_path)
{
    const auto relative=std::filesystem::u8path(relative_path);
    if (relative.empty() || relative.is_absolute()) throw std::invalid_argument("Choose an unused managed file");
    for (const auto& part:relative) if (part=="." || part=="..") throw std::invalid_argument("Invalid cleanup path");
    files::WorkspaceLock lock(directory_,true); sql::Transaction transaction(database_.get());
    const auto issues=audit_attachments_unlocked();
    const auto found=std::find_if(issues.begin(),issues.end(),[&](const auto& issue){return issue.path==relative_path && issue.removable;});
    if (found==issues.end()) throw std::runtime_error("This file is referenced, unsafe, or no longer unused; refresh the audit");
    transaction.commit();
    if (!std::filesystem::remove(directory_/"attachments"/relative)) throw std::runtime_error("Unused file is missing; refresh the audit");
    files::sync_directory(directory_/"attachments");
}
PurgeReport Workspace::empty_trash()
{
    sqlite3* db=database_.get(); PurgeReport report;
    files::WorkspaceLock lock(directory_,true);
    std::vector<std::string> purged_files;
    {
        sql::Transaction transaction(db);
        std::vector<std::string> pages; std::set<std::string> purged;
        {
            sql::Statement query(db,"SELECT p.id FROM pages p JOIN databases d ON d.id=p.database_id WHERE p.trashed OR d.trashed");
            while (query.row()) { pages.push_back(query.text(0)); purged.insert(query.text(0)); }
        }
        // Surviving pages whose default labels showed a purged title or block fall back to stored text.
        std::set<std::string> referrers;
        for (const auto& page:pages) {
            sql::Statement query(db,"SELECT DISTINCT source_page FROM links WHERE (target_kind=0 AND target_id=?) OR (target_kind=1 AND target_id IN (SELECT id FROM blocks WHERE page_id=?))");
            query.bind(1,page).bind(2,page);
            while (query.row()) if (!purged.count(query.text(0))) referrers.insert(query.text(0));
        }
        report.fragments=std::size_t(std::stoll(sql::scalar(db,"SELECT count(*) FROM deleted_blocks")));
        sql::execute(db,"DELETE FROM deleted_blocks");
        sql::Statement remove_page(db,"DELETE FROM pages WHERE id=?");
        for (const auto& page:pages) { storage_detail::remove_page_search(db,page); remove_page.reset().bind(1,page).done(); }
        report.pages=pages.size();
        std::vector<std::string> trashed;
        { sql::Statement query(db,"SELECT id FROM databases WHERE trashed"); while (query.row()) trashed.push_back(query.text(0)); }
        for (const auto& database:trashed) {
            for (const char* command:{"DELETE FROM saved_views WHERE database_id=?",
                    "DELETE FROM property_values WHERE property_id IN (SELECT id FROM property_definitions WHERE database_id=?)",
                    "DELETE FROM property_definitions WHERE database_id=?","DELETE FROM databases WHERE id=?"}) {
                sql::Statement statement(db,command); statement.bind(1,database).done();
            }
        }
        report.databases=trashed.size();
        for (const auto& page:referrers) storage_detail::index_page(db,load_page(page));
        // With trash and recovery gone, registered files nothing references are released.
        std::unordered_set<std::string> referenced;
        { sql::Statement query(db,"SELECT id FROM pages"); while (query.row()) collect(load_page(query.text(0)).document,referenced); }
        sql::Statement unregister(db,"DELETE FROM attachments WHERE id=?");
        for (const auto& file:attachments()) if (!referenced.count(file.id)) { unregister.reset().bind(1,file.id).done(); purged_files.push_back(file.id); }
        transaction.commit();
    }
    for (const auto& id:purged_files) {
        std::error_code ignored;
        if (std::filesystem::remove(directory_/"attachments"/id,ignored)) ++report.files;
    }
    if (!purged_files.empty()) files::sync_directory(directory_/"attachments");
    // Free pages still hold deleted text until the file is rewritten.
    try { sql::execute(db,"VACUUM"); report.compacted=true; } catch (const std::exception&) { report.compacted=false; }
    return report;
}
} // namespace nebula
