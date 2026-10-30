#include "nebula/storage/workspace.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/format/codec.hpp"
#include "sql.hpp"
#include <algorithm>
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
    if (!attachment(id)) throw std::runtime_error("Attachment metadata is unavailable: "+id);
    const auto directory=directory_/"attachments"; check_directory(directory); const auto path=directory/id;
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Managed attachment file is missing or unsafe: "+id);
    return path;
}
ImportedAttachment Workspace::import_attachment(PageRecord& page, const std::string& after,
    const std::filesystem::path& source, bool image)
{
    const auto directory=directory_/"attachments"; check_directory(directory);
    files::WorkspaceLock lock(directory_,true); sql::Transaction transaction(database_.get());
    if (resolve_reference({ReferenceKind::Page,page.document.id}).state!=ReferenceState::Available)
        throw std::runtime_error("Restore the page and its database before importing files");
    const auto identity=sql::id(database_.get()); const auto stage=directory/(".import-"+identity), destination=directory/identity;
    bool staged=false, promoted=false, committed=false;
    try {
        const auto digest=files::copy_new(source,stage);
        staged=true;
        // Link creates the final name without overwriting an existing identity.
        std::filesystem::create_hard_link(stage,destination); promoted=true;
        std::filesystem::remove(stage); staged=false; files::sync_directory(directory);
        const AttachmentInfo file{identity,source.filename().u8string(),digest.sha256,digest.size,image};
        if (file.name.empty()) throw std::invalid_argument("Choose a named file");
        sql::Statement add(database_.get(),"INSERT INTO attachments(id,name,sha256,size,image) VALUES(?,?,?,?,?)");
        add.bind(1,file.id).bind(2,file.name).bind(3,file.sha256).bind(4,file.size).bind(5,file.image).done();
        auto next=page; const auto card=sql::id(database_.get()), following=sql::id(database_.get());
        insert_attachment(next.document,after,file.id,file.name,image,card,following);
        ImportedAttachment result{file,card,following};
        save_page_in_transaction(next); transaction.commit(); committed=true; ++next.revision; page=std::move(next);
        return result;
    } catch (...) {
        std::error_code ignored; if (staged) std::filesystem::remove(stage,ignored);
        if (promoted && !committed) std::filesystem::remove(destination,ignored);
        throw;
    }
}
std::vector<AttachmentIssue> Workspace::audit_attachments_unlocked() const
{
    const auto directory=directory_/"attachments"; check_directory(directory);
    std::unordered_set<std::string> referenced;
    sql::Statement pages(database_.get(),"SELECT id FROM pages");
    while (pages.row()) collect(load_page(pages.text(0)).document,referenced);
    // Retain all recovery fragments, including fragments already restored. They
    // remain user-owned recovery records until an explicit purge policy exists.
    sql::Statement fragments(database_.get(),"SELECT fragment FROM deleted_blocks");
    while (fragments.row()) collect(decode_document(fragments.text(0)),referenced);
    std::unordered_map<std::string,AttachmentInfo> known; std::vector<AttachmentIssue> issues;
    for (const auto& file:attachments()) {
        known.emplace(file.id,file); const auto path=directory/file.id;
        if (!referenced.count(file.id)) issues.push_back({file.id,file.id,"Unreferenced imported file retained for undo and recovery",false,false});
        try {
            const auto actual=files::digest(path);
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
} // namespace nebula
