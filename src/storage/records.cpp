#include "nebula/storage/workspace.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/storage/sql.hpp"
#include "records_internal.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {
using sql::Statement;
// Imported revisions are bounded well below int64 overflow (see JSON import).
constexpr std::int64_t RevisionLimit = std::int64_t(1) << 62;
void check_revision(std::int64_t revision) {
    if (revision<1 || revision>=RevisionLimit)
        throw StorageError(ErrorKind::Invalid,"This page's revision is outside the supported range; export and re-import the workspace");
}
LinkEdge read_edge(const Statement& row, const Workspace& workspace) {
    LinkEdge edge; edge.source_page=row.text(0); edge.source_block=row.text(1);
    edge.source_title=row.text(2); edge.target_id=row.text(3); edge.kind=ReferenceKind(row.number(4));
    edge.run_index=row.number(5); edge.position=row.number(6);
    Block block; decode_block(block,row.text(7));
    auto label=[&](const Run& run) { return run.reference && !run.reference->custom_label
        ?workspace.reference_label(*run.reference).value_or(run.text):run.text; };
    for (const auto& run:block.runs) edge.source_context+=label(run);
    if (edge.source_context.empty()) edge.source_context=block.asset_label;
    if (edge.run_index>=0 && std::size_t(edge.run_index)<block.runs.size()) edge.label=label(block.runs[std::size_t(edge.run_index)]);
    return edge;
}
void name_required(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos) throw std::invalid_argument("A nonempty name is required");
}
void insert_blocks(sqlite3* db, const Page& page) {
    Statement add(db, "INSERT INTO blocks(id,page_id,parent_id,position,kind,payload) VALUES(?,?,nullif(?,''),?,?,?)");
    for (std::size_t i = 0; i < page.blocks.size(); ++i) {
        const auto& block = page.blocks[i];
        add.reset().bind(1,block.id).bind(2,page.id).bind(3,block.parent_id).bind(4,std::int64_t(i))
            .bind(5,int(block.kind)).bind(6,encode_block(block)).done();
        storage_detail::insert_links(db,page.id,block);
    }
}
void archive_removed(sqlite3* db,const Page& previous,const Page& next,const std::unordered_set<std::string>& absorbed) {
    std::unordered_set<std::string> live,removed;
    for (const auto& block:next.blocks) live.insert(block.id);
    // Merged blocks survive inside another block; archiving them would put
    // text that still exists into Restore….
    for (const auto& block:previous.blocks) if (!live.count(block.id) && !absorbed.count(block.id)) removed.insert(block.id);
    const auto operation=removed.empty()?std::string{}:sql::id(db);
    Statement add(db,"INSERT INTO deleted_blocks(id,page_id,parent_id,anchor_id,fragment,operation_id,original_position) VALUES(?,?,?,?,?,?,?)");
    for (std::size_t i=0;i<previous.blocks.size();++i) {
        const auto& root=previous.blocks[i];
        if (!removed.count(root.id) || removed.count(root.parent_id)) continue;
        Page fragment{previous.id,{root}}; fragment.blocks[0].parent_id.clear();
        std::unordered_set<std::string> descendants{root.id},included{root.id};
        auto end=i+1;
        while (end<previous.blocks.size() && descendants.count(previous.blocks[end].parent_id)) {
            const auto& child=previous.blocks[end++]; descendants.insert(child.id);
            if (removed.count(child.id) && included.count(child.parent_id)) { fragment.blocks.push_back(child); included.insert(child.id); }
        }
        std::string anchor;
        for (auto j=end;j<previous.blocks.size();++j)
            if (previous.blocks[j].parent_id==root.parent_id && live.count(previous.blocks[j].id)) { anchor=previous.blocks[j].id; break; }
        const auto identity=sql::id(db);
        add.reset().bind(1,identity).bind(2,previous.id).bind(3,root.parent_id).bind(4,anchor).bind(5,encode_document(fragment))
            .bind(6,operation).bind(7,std::int64_t(i)).done();
        storage_detail::add_recovery_members(db,identity,fragment);
    }
    // Fragments whose blocks are all live again (for example after undo) are restored.
    Statement members(db,"SELECT m.fragment_id,m.block_id FROM deleted_block_members m JOIN deleted_blocks f ON f.id=m.fragment_id WHERE f.page_id=? AND NOT f.restored");
    members.bind(1,next.id);
    std::unordered_map<std::string,bool> complete;
    while (members.row()) {
        auto& state=complete.emplace(members.text(0),true).first->second;
        if (!live.count(members.text(1))) state=false;
    }
    Statement done(db,"UPDATE deleted_blocks SET restored=1 WHERE id=?");
    for (const auto& entry:complete) if (entry.second) done.reset().bind(1,entry.first).done();
}
} // namespace
PageRecord storage_detail::load_summary(sqlite3* db,const std::string& id) {
    Statement query(db,"SELECT database_id,title,revision,trashed FROM pages WHERE id=?"); query.bind(1,id);
    if (!query.row()) throw StorageError(ErrorKind::Unavailable,"Page not found");
    PageRecord page{{id,{}},query.text(0),query.text(1),{},query.number(2),bool(query.number(3))};
    Statement values(db,"SELECT property_id,value_json FROM property_values WHERE page_id=?"); values.bind(1,id);
    while (values.row()) page.properties.emplace(values.text(0),decode_value(values.text(1)));
    return page;
}
void storage_detail::insert_links(sqlite3* db,const std::string& page,const Block& block) {
    Statement edge(db,"INSERT INTO links(source_page,source_block,run_index,target_kind,target_id) VALUES(?,?,?,?,?)");
    for (std::size_t r=0;r<block.runs.size();++r) {
        const auto& ref=block.runs[r].reference;
        if (!ref || ref->target_id.empty()) continue;
        edge.reset().bind(1,page).bind(2,block.id).bind(3,std::int64_t(r)).bind(4,int(ref->kind)).bind(5,ref->target_id).done();
    }
}
void storage_detail::add_recovery_members(sqlite3* db,const std::string& fragment,const Page& blocks) {
    Statement add(db,"INSERT OR IGNORE INTO deleted_block_members(fragment_id,block_id) VALUES(?,?)");
    for (const auto& block:blocks.blocks) add.reset().bind(1,fragment).bind(2,block.id).done();
}
void Workspace::transaction(const std::function<void()>& work)
{
    sql::Transaction transaction(database_.get()); work(); transaction.commit();
}
Database Workspace::create_database(const std::string& name)
{
    name_required(name);
    Database result{sql::id(database_.get()), name, false};
    Statement add(database_.get(), "INSERT INTO databases(id,name) VALUES(?,?)");
    add.bind(1,result.id).bind(2,name).done(); return result;
}
Database Workspace::create_journal(const std::string& name)
{
    sql::Transaction transaction(database_.get());
    const auto db=create_database(name);
    View all; all.database_id=db.id; all.name="All entries";
    all.visible_properties.push_back(add_property(db.id,"Entry date",PropertyKind::Date).id);
    all.visible_properties.push_back(add_property(db.id,"Mood",PropertyKind::Text).id);
    all.visible_properties.push_back(add_property(db.id,"Tags",PropertyKind::Tags).id);
    save_view(all); transaction.commit(); return db;
}
std::vector<Database> Workspace::databases(bool include_trashed) const
{
    Statement query(database_.get(), "SELECT id,name,trashed FROM databases WHERE ? OR NOT trashed ORDER BY name,id");
    query.bind(1,include_trashed);
    std::vector<Database> result;
    while (query.row()) result.push_back({query.text(0),query.text(1),bool(query.number(2))});
    return result;
}
void Workspace::rename_database(const std::string& id, const std::string& name)
{
    name_required(name);
    Statement update(database_.get(), "UPDATE databases SET name=? WHERE id=?");
    update.bind(1,name).bind(2,id).done(); sql::changed(database_.get());
}
void Workspace::trash_database(const std::string& id, bool trashed)
{
    Statement update(database_.get(), "UPDATE databases SET trashed=? WHERE id=?");
    update.bind(1,trashed).bind(2,id).done(); sql::changed(database_.get());
}
PropertyDefinition Workspace::add_property(const std::string& database, const std::string& name, PropertyKind kind)
{
    name_required(name);
    if (int(kind) < 0 || int(kind) > 4) throw std::invalid_argument("Unsupported property type");
    PropertyDefinition result{sql::id(database_.get()),database,name,kind};
    Statement add(database_.get(), "INSERT INTO property_definitions(id,database_id,name,kind) VALUES(?,?,?,?)");
    add.bind(1,result.id).bind(2,database).bind(3,name).bind(4,int(kind)).done(); return result;
}
void Workspace::rename_property(const std::string& id, const std::string& name)
{
    name_required(name);
    sql::Transaction transaction(database_.get());
    Statement update(database_.get(),"UPDATE property_definitions SET name=? WHERE id=?"); update.bind(1,name).bind(2,id).done();
    sql::changed(database_.get());
    // Property names are part of the searchable page row.
    Statement pages(database_.get(),"SELECT page_id FROM property_values WHERE property_id=?"); pages.bind(1,id);
    std::vector<std::string> affected; while (pages.row()) affected.push_back(pages.text(0));
    storage_detail::SearchChange change; change.page_row=true;
    for (const auto& page:affected) storage_detail::refresh_search(database_.get(),storage_detail::load_summary(database_.get(),page),change);
    transaction.commit();
}
void Workspace::delete_property(const std::string& id)
{
    sql::Transaction transaction(database_.get()); sqlite3* db=database_.get();
    Statement owner(db,"SELECT database_id FROM property_definitions WHERE id=?"); owner.bind(1,id);
    if (!owner.row()) throw StorageError(ErrorKind::Unavailable,"This property no longer exists");
    const auto database=owner.text(0);
    Statement pages(db,"SELECT page_id FROM property_values WHERE property_id=?"); pages.bind(1,id);
    std::vector<std::string> affected; while (pages.row()) affected.push_back(pages.text(0));
    Statement values(db,"DELETE FROM property_values WHERE property_id=?"); values.bind(1,id).done();
    // Editors holding the old value must reload instead of writing it back.
    Statement bump(db,"UPDATE pages SET revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?");
    for (const auto& page:affected) bump.reset().bind(1,page).done();
    for (auto view:views(database)) {
        view.visible_properties.erase(std::remove(view.visible_properties.begin(),view.visible_properties.end(),id),view.visible_properties.end());
        view.filters.erase(std::remove_if(view.filters.begin(),view.filters.end(),[&](const Filter& f){return f.property_id==id;}),view.filters.end());
        if (view.sort_property==id) view.sort_property.clear();
        Statement update(db,"UPDATE saved_views SET settings=? WHERE id=?"); update.bind(1,encode_view(view)).bind(2,view.id).done();
    }
    Statement remove(db,"DELETE FROM property_definitions WHERE id=?"); remove.bind(1,id).done();
    storage_detail::SearchChange change; change.page_row=true;
    for (const auto& page:affected) storage_detail::refresh_search(db,storage_detail::load_summary(db,page),change);
    transaction.commit();
}
std::vector<PropertyDefinition> Workspace::properties(const std::string& database) const
{
    Statement query(database_.get(), "SELECT id,name,kind FROM property_definitions WHERE database_id=? ORDER BY rowid");
    query.bind(1,database); std::vector<PropertyDefinition> result;
    while (query.row()) result.push_back({query.text(0),database,query.text(1),PropertyKind(query.number(2))});
    return result;
}
PageRecord Workspace::create_page(const std::string& database, const std::string& title,
                                 const std::map<std::string,PropertyValue>& values,
                                 const std::vector<Block>& initial_blocks)
{
    sql::Transaction transaction(database_.get());
    auto page=create_page_in_transaction(database,title,values,initial_blocks);
    transaction.commit(); return page;
}
PageRecord Workspace::create_dated_entry(const std::string& database, const std::string& date)
{
    if (!valid_date(date)) throw std::invalid_argument("Enter a valid YYYY-MM-DD date");
    sql::Transaction transaction(database_.get());
    const auto definitions=properties(database);
    const auto found=std::find_if(definitions.begin(),definitions.end(),[](const auto& d){ return d.name=="Entry date" && d.kind==PropertyKind::Date; });
    const auto property=found==definitions.end()?add_property(database,"Entry date",PropertyKind::Date).id:found->id;
    auto page=create_page_in_transaction(database,date,{{property,Date{date}}},{});
    transaction.commit(); return page;
}
PageRecord Workspace::create_page_in_transaction(const std::string& database, const std::string& title,
    const std::map<std::string,PropertyValue>& values, const std::vector<Block>& initial_blocks,
    const std::string& identity)
{
    const auto page_id = identity.empty()?sql::id(database_.get()):identity;
    PageRecord page{{page_id, {{sql::id(database_.get()), "", BlockKind::Paragraph, {}}}}, database, title, values, 1, false};
    if (!initial_blocks.empty()) page.document.blocks=initial_blocks;
    validate(page.document);
    const auto defs=properties(database);
    for (const auto& entry:values) {
        const auto found=std::find_if(defs.begin(),defs.end(),[&](const auto& def){return def.id==entry.first;});
        if (found==defs.end()) throw std::invalid_argument("Property does not belong to this database");
        validate_value(found->kind,entry.second);
    }
    Statement add(database_.get(), "INSERT INTO pages(id,database_id,title,revision) VALUES(?,?,?,1)");
    add.bind(1,page_id).bind(2,database).bind(3,title).done();
    insert_blocks(database_.get(),page.document);
    Statement add_value(database_.get(),"INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?)");
    for (const auto& entry:values) add_value.reset().bind(1,page_id).bind(2,entry.first).bind(3,encode_value(entry.second)).done();
    storage_detail::index_page(database_.get(),page);
    // A recreated identity (for example redo of an explicit creation) refreshes default labels.
    storage_detail::reindex_referrers(database_.get(),page_id,true,{});
    return page;
}
PageRecord Workspace::load_page(const std::string& id) const
{
    return storage_detail::load_page(database_.get(),id);
}
PageRecord storage_detail::load_page(sqlite3* database,const std::string& id)
{
    auto page=storage_detail::load_summary(database,id);
    Statement blocks(database, "SELECT id,parent_id,kind,payload FROM blocks WHERE page_id=? ORDER BY position");
    blocks.bind(1,id);
    while (blocks.row()) {
        Block block{blocks.text(0),blocks.text(1),BlockKind(blocks.number(2))};
        decode_block(block,blocks.text(3)); page.document.blocks.push_back(std::move(block));
    }
    validate(page.document);
    return page;
}
std::optional<std::string> Workspace::page_title(const std::string& id) const
{
    Statement query(database_.get(),"SELECT title FROM pages WHERE id=?"); query.bind(1,id);
    if (!query.row()) return {};
    return query.text(0);
}
std::vector<PageRecord> Workspace::pages(const std::string& database, bool include_trashed) const
{
    Statement query(database_.get(), "SELECT p.id FROM pages p JOIN databases d ON d.id=p.database_id WHERE p.database_id=? AND (? OR (NOT p.trashed AND NOT d.trashed)) ORDER BY p.created_at,p.id");
    query.bind(1,database).bind(2,include_trashed); std::vector<PageRecord> result;
    while (query.row()) result.push_back(load_page(query.text(0)));
    return result;
}
std::vector<PageSummary> Workspace::page_summaries(bool include_trashed) const
{
    Statement query(database_.get(),"SELECT p.id,p.title,p.database_id,d.name,p.trashed,d.trashed FROM pages p JOIN databases d ON d.id=p.database_id WHERE ? OR (NOT p.trashed AND NOT d.trashed) ORDER BY d.name,d.id,p.created_at,p.id");
    query.bind(1,include_trashed); std::vector<PageSummary> result;
    while (query.row()) result.push_back({query.text(0),query.text(1),query.text(2),query.text(3),bool(query.number(4)),bool(query.number(5))});
    return result;
}
std::vector<LinkTargetRow> Workspace::link_targets() const
{
    Statement query(database_.get(),"SELECT p.id,p.title,d.name,b.id,b.kind,b.payload FROM pages p JOIN databases d ON d.id=p.database_id JOIN blocks b ON b.page_id=p.id WHERE NOT p.trashed AND NOT d.trashed ORDER BY d.name,d.id,p.created_at,p.id,b.position");
    std::vector<LinkTargetRow> result; std::string current;
    while (query.row()) {
        if (query.text(0)!=current) {
            current=query.text(0);
            result.push_back({ReferenceKind::Page,current,query.text(1),current,query.text(1),query.text(2),false});
        }
        Block block{query.text(3),{},BlockKind(query.number(4))}; decode_block(block,query.text(5));
        std::string text; for (const auto& run:block.runs) text+=run.text;
        if (text.empty()) text=block.asset_label.empty()?"Empty block":block.asset_label;
        result.push_back({ReferenceKind::Block,block.id,text,current,query.text(1),query.text(2),block.kind==BlockKind::Heading});
    }
    return result;
}
void Workspace::save_page(PageRecord& page, const std::unordered_set<std::string>& absorbed)
{
    sql::Transaction transaction(database_.get());
    save_page_in_transaction(page,absorbed); transaction.commit(); ++page.revision;
}
// Writes only what changed: blocks are matched by identity, unchanged rows and
// their links/search rows stay untouched, and referrers are reindexed only when
// a label they display changes.
void Workspace::save_page_in_transaction(PageRecord& page, const std::unordered_set<std::string>& absorbed)
{
    sqlite3* db=database_.get();
    validate(page.document); check_revision(page.revision);
    {
        // Report a stale editor as a conflict before validating its other values.
        Statement current(db,"SELECT revision FROM pages WHERE id=?"); current.bind(1,page.document.id);
        if (!current.row() || current.number(0)!=page.revision)
            throw StorageError(ErrorKind::Conflict,"Record is missing or changed in another window; reopen it before saving");
    }
    const auto definitions = properties(page.database_id);
    for (const auto& entry : page.properties) {
        const auto def = std::find_if(definitions.begin(),definitions.end(),[&](const auto& d) { return d.id == entry.first; });
        if (def == definitions.end()) throw std::invalid_argument("Property does not belong to this database");
        validate_value(def->kind,entry.second);
    }
    struct Stored { std::string parent; std::int64_t position; int kind; std::string payload; };
    std::unordered_map<std::string,Stored> stored; std::vector<std::string> order;
    {
        Statement rows(db,"SELECT id,parent_id,position,kind,payload FROM blocks WHERE page_id=? ORDER BY position"); rows.bind(1,page.document.id);
        while (rows.row()) { order.push_back(rows.text(0)); stored.emplace(rows.text(0),Stored{rows.text(1),rows.number(2),int(rows.number(3)),rows.text(4)}); }
    }
    std::string stored_title;
    {
        Statement title(db,"SELECT title FROM pages WHERE id=?"); title.bind(1,page.document.id);
        if (!title.row()) throw StorageError(ErrorKind::Conflict,"Record is missing or changed in another window; reopen it before saving");
        stored_title=title.text(0);
    }
    Statement update(db, "UPDATE pages SET database_id=?,title=?,trashed=?,revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND revision=?");
    update.bind(1,page.database_id).bind(2,page.title).bind(3,page.trashed).bind(4,page.document.id).bind(5,page.revision).done();
    sql::changed(db);
    std::unordered_set<std::string> live; for (const auto& block:page.document.blocks) live.insert(block.id);
    Page previous{page.document.id,{}};
    for (const auto& id:order) {
        const auto& row=stored.at(id); Block block{id,row.parent,BlockKind(row.kind)};
        if (!live.count(id)) decode_block(block,row.payload); // Only removed blocks are archived.
        previous.blocks.push_back(std::move(block));
    }
    archive_removed(db,previous,page.document,absorbed);
    storage_detail::SearchChange change;
    Statement remove(db,"DELETE FROM blocks WHERE id=?");
    for (const auto& id:order) if (!live.count(id)) {
        remove.reset().bind(1,id).done(); change.removed.push_back(id); change.label_blocks.push_back(id);
    }
    // Move reordered rows out of the way first; UNIQUE(page_id,position) holds throughout.
    const auto offset=std::int64_t(order.size()+page.document.blocks.size())+1;
    Statement shift(db,"UPDATE blocks SET position=position+? WHERE id=?");
    for (std::size_t i=0;i<page.document.blocks.size();++i) {
        const auto found=stored.find(page.document.blocks[i].id);
        if (found!=stored.end() && found->second.position!=std::int64_t(i)) shift.reset().bind(1,offset).bind(2,found->first).done();
    }
    Statement insert(db,"INSERT INTO blocks(id,page_id,parent_id,position,kind,payload) VALUES(?,?,nullif(?,''),?,?,?)");
    Statement replace(db,"UPDATE blocks SET parent_id=nullif(?,''),position=?,kind=?,payload=? WHERE id=?");
    Statement unlink(db,"DELETE FROM links WHERE source_block=?");
    for (std::size_t i=0;i<page.document.blocks.size();++i) {
        const auto& block=page.document.blocks[i]; const auto payload=encode_block(block);
        const auto found=stored.find(block.id);
        if (found==stored.end()) {
            insert.reset().bind(1,block.id).bind(2,page.document.id).bind(3,block.parent_id).bind(4,std::int64_t(i))
                .bind(5,int(block.kind)).bind(6,payload).done();
            storage_detail::insert_links(db,page.document.id,block); change.blocks.push_back(block.id);
            continue;
        }
        const auto& row=found->second;
        const bool content=row.payload!=payload || row.kind!=int(block.kind);
        if (!content && row.parent==block.parent_id && row.position==std::int64_t(i)) continue;
        replace.reset().bind(1,block.parent_id).bind(2,std::int64_t(i)).bind(3,int(block.kind)).bind(4,payload).bind(5,block.id).done();
        if (!content) continue;
        unlink.reset().bind(1,block.id).done(); storage_detail::insert_links(db,page.document.id,block);
        change.blocks.push_back(block.id);
        Block old{block.id}; decode_block(old,row.payload);
        if (reference_title(old)!=reference_title(block)) change.label_blocks.push_back(block.id);
    }
    std::map<std::string,std::string> stored_values;
    {
        Statement values(db,"SELECT property_id,value_json FROM property_values WHERE page_id=?"); values.bind(1,page.document.id);
        while (values.row()) stored_values.emplace(values.text(0),values.text(1));
    }
    bool properties_changed=false;
    Statement upsert(db,"INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?) ON CONFLICT(page_id,property_id) DO UPDATE SET value_json=excluded.value_json");
    for (const auto& entry:page.properties) {
        const auto encoded=encode_value(entry.second); const auto found=stored_values.find(entry.first);
        if (found!=stored_values.end() && found->second==encoded) continue;
        upsert.reset().bind(1,page.document.id).bind(2,entry.first).bind(3,encoded).done(); properties_changed=true;
    }
    Statement drop(db,"DELETE FROM property_values WHERE page_id=? AND property_id=?");
    for (const auto& entry:stored_values) if (!page.properties.count(entry.first)) {
        drop.reset().bind(1,page.document.id).bind(2,entry.first).done(); properties_changed=true;
    }
    change.label_title=stored_title!=page.title;
    change.page_row=properties_changed || change.label_title;
    storage_detail::refresh_search(db,page,change);
}
void Workspace::trash_page(const std::string& id, bool trashed)
{
    Statement update(database_.get(), "UPDATE pages SET trashed=?,revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?");
    update.bind(1,trashed).bind(2,id).done(); sql::changed(database_.get());
}
ReferenceTarget Workspace::resolve_reference(const Reference& reference) const
{
    if (reference.target_id.empty()) return {ReferenceState::Unresolved,{},{}};
    Statement query(database_.get(), reference.kind==ReferenceKind::Page
        ? "SELECT p.id,'',p.trashed,d.trashed FROM pages p JOIN databases d ON d.id=p.database_id WHERE p.id=?"
        : "SELECT p.id,b.id,p.trashed,d.trashed FROM blocks b JOIN pages p ON p.id=b.page_id JOIN databases d ON d.id=p.database_id WHERE b.id=?");
    query.bind(1,reference.target_id);
    if (!query.row()) return {};
    const auto state=query.number(3)?ReferenceState::DatabaseTrashed
        :query.number(2)?ReferenceState::PageTrashed:ReferenceState::Available;
    return {state,query.text(0),query.text(1)};
}
std::optional<std::string> Workspace::reference_label(const Reference& reference) const
{
    return storage_detail::reference_label(database_.get(),reference);
}
std::optional<std::string> storage_detail::reference_label(sqlite3* database,const Reference& reference)
{
    if (reference.target_id.empty()) return reference.unresolved_name;
    Statement query(database,reference.kind==ReferenceKind::Page
        ?"SELECT title FROM pages WHERE id=?":"SELECT payload FROM blocks WHERE id=?");
    query.bind(1,reference.target_id); if (!query.row()) return {};
    if (reference.kind==ReferenceKind::Page) return query.text(0);
    Block block; decode_block(block,query.text(0)); return reference_title(block);
}
PageRecord Workspace::create_reference_target(PageRecord& source, const ReferenceLocation& location,
                                              const std::string& database, const std::string& title)
{
    name_required(title); auto next=source;
    auto block=std::find_if(next.document.blocks.begin(),next.document.blocks.end(),[&](const auto& b){return b.id==location.block;});
    if (block==next.document.blocks.end() || !location.run_count || location.first_run>=block->runs.size()
        || location.run_count>block->runs.size()-location.first_run) throw std::invalid_argument("Select an unresolved reference first");
    const auto first=location.first_run, end=first+location.run_count;
    const auto reference=block->runs[first].reference;
    if (!reference || !reference->target_id.empty()) throw std::invalid_argument("This reference is already resolved");
    for (auto i=first;i<end;++i)
        if (!block->runs[i].reference || *block->runs[i].reference!=*reference)
            throw std::invalid_argument("Select a single unresolved reference");
    sql::Transaction transaction(database_.get());
    if (resolve_reference({ReferenceKind::Page,source.document.id}).state!=ReferenceState::Available)
        throw std::runtime_error("Restore the source page and database first");
    Statement target_db(database_.get(),"SELECT id FROM databases WHERE id=? AND NOT trashed"); target_db.bind(1,database);
    if (!target_db.row()) throw std::runtime_error("Choose an available destination database");
    auto target=create_page_in_transaction(database,title,{},{});
    Reference resolved{ReferenceKind::Page,target.document.id,"","",reference->custom_label};
    if (reference->custom_label) {
        for (auto i=first;i<end;++i) block->runs[i].reference=resolved;
    } else {
        auto run=block->runs[first]; run.text=title; run.reference=resolved;
        block->runs.erase(block->runs.begin()+first,block->runs.begin()+end);
        block->runs.insert(block->runs.begin()+first,std::move(run));
    }
    save_page_in_transaction(next); transaction.commit(); ++next.revision; source=std::move(next);
    return target;
}
void Workspace::move_page(PageRecord& page, const std::string& database)
{
    sql::Transaction transaction(database_.get());
    const auto previous=load_page(page.document.id);
    if (page.revision!=previous.revision) throw StorageError(ErrorKind::Conflict,"Page changed in another window; reopen it before moving");
    check_revision(page.revision);
    if (resolve_reference({ReferenceKind::Page,page.document.id}).state!=ReferenceState::Available)
        throw std::runtime_error("Restore the page and its database before moving it");
    Statement target(database_.get(),"SELECT id FROM databases WHERE id=? AND NOT trashed"); target.bind(1,database);
    if (!target.row()) throw std::runtime_error("Destination database is unavailable or in trash");
    if (previous.database_id==database) { transaction.commit(); return; }
    const auto source_definitions=properties(previous.database_id);
    auto target_definitions=properties(database);
    std::map<std::string,PropertyValue> values;
    for (const auto& entry:previous.properties) {
        const auto definition=std::find_if(source_definitions.begin(),source_definitions.end(),[&](const auto& d){return d.id==entry.first;});
        if (definition==source_definitions.end()) throw std::runtime_error("Source property definition is unavailable");
        auto found=std::find_if(target_definitions.begin(),target_definitions.end(),[&](const auto& d){return d.name==definition->name;});
        if (found==target_definitions.end()) {
            target_definitions.push_back(add_property(database,definition->name,definition->kind));
            found=target_definitions.end()-1;
        }
        if (found->kind!=definition->kind) throw std::runtime_error("Destination property has a different type: "+definition->name);
        validate_value(found->kind,entry.second); values.emplace(found->id,entry.second);
    }
    Statement update(database_.get(),"UPDATE pages SET database_id=?,revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND revision=?");
    update.bind(1,database).bind(2,page.document.id).bind(3,page.revision).done(); sql::changed(database_.get());
    Statement clear(database_.get(),"DELETE FROM property_values WHERE page_id=?"); clear.bind(1,page.document.id).done();
    Statement add(database_.get(),"INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?)");
    for (const auto& entry:values) add.reset().bind(1,page.document.id).bind(2,entry.first).bind(3,encode_value(entry.second)).done();
    auto next=previous; next.database_id=database; next.properties=std::move(values); ++next.revision;
    storage_detail::SearchChange change; change.page_row=true; storage_detail::refresh_search(database_.get(),next,change);
    transaction.commit(); page=std::move(next);
}
void Workspace::replace_transfer_documents(const Page& source, std::int64_t source_revision,
                                           const Page& destination, std::int64_t destination_revision)
{
    validate(source); validate(destination);
    if (source.id==destination.id) throw std::invalid_argument("Choose a different destination page");
    check_revision(source_revision); check_revision(destination_revision);
    sql::Transaction transaction(database_.get());
    for (const auto& entry:{std::pair<const Page*,std::int64_t>{&source,source_revision},{&destination,destination_revision}}) {
        if (resolve_reference({ReferenceKind::Page,entry.first->id}).state!=ReferenceState::Available)
            throw std::runtime_error("Restore both pages and their databases before transferring blocks");
        Statement update(database_.get(),"UPDATE pages SET revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND revision=?");
        update.bind(1,entry.first->id).bind(2,entry.second).done(); sql::changed(database_.get());
    }
    // Remove both old trees before inserting either tree: global block identities
    // change owners, and their outgoing edges must follow them in this commit.
    for (const auto* page:{&source,&destination}) {
        Statement remove(database_.get(),"DELETE FROM blocks WHERE page_id=?"); remove.bind(1,page->id).done();
    }
    insert_blocks(database_.get(),source); insert_blocks(database_.get(),destination);
    for (const auto* page:{&source,&destination}) storage_detail::index_page(database_.get(),load_page(page->id));
    transaction.commit();
}
BlockTransfer Workspace::transfer_blocks(const PageRecord& source, const PageRecord& destination,
                                         const std::string& block)
{
    BlockTransfer transfer{source.document,destination.document,source.document,destination.document,
                           source.revision,destination.revision,true};
    transfer_subtree(transfer.source_after,transfer.destination_after,block,sql::id(database_.get()));
    replace_transfer_documents(transfer.source_after,transfer.source_revision,
                               transfer.destination_after,transfer.destination_revision);
    ++transfer.source_revision; ++transfer.destination_revision;
    return transfer;
}
void Workspace::set_transfer_applied(BlockTransfer& transfer, bool applied)
{
    if (transfer.applied==applied) return;
    replace_transfer_documents(applied?transfer.source_after:transfer.source_before,transfer.source_revision,
                               applied?transfer.destination_after:transfer.destination_before,transfer.destination_revision);
    ++transfer.source_revision; ++transfer.destination_revision; transfer.applied=applied;
}
std::vector<DeletedBlockGroup> Workspace::deleted_blocks() const
{
    Statement query(database_.get(),"SELECT t.id,t.page_id,p.title,t.fragment FROM deleted_blocks t JOIN pages p ON p.id=t.page_id WHERE NOT t.restored ORDER BY t.removed_at,t.rowid");
    std::vector<DeletedBlockGroup> result;
    while (query.row()) {
        const auto fragment=decode_document(query.text(3));
        std::string label=fragment.blocks[0].asset_label;
        if (label.empty()) for (const auto& run:fragment.blocks[0].runs) label+=run.text;
        result.push_back({query.text(0),query.text(1),query.text(2),label.empty()?"Empty block":label});
    }
    return result;
}
PageRecord Workspace::restore_deleted_blocks(const std::string& id, bool* reparented)
{
    if (reparented) *reparented=false;
    std::string page_id,parent,anchor,payload,operation;
    std::int64_t original_position=0;
    {
        Statement query(database_.get(),"SELECT page_id,parent_id,anchor_id,fragment,operation_id,original_position FROM deleted_blocks WHERE id=? AND NOT restored"); query.bind(1,id);
        if (!query.row()) throw StorageError(ErrorKind::Unavailable,"Deleted blocks are already restored or unavailable");
        page_id=query.text(0); parent=query.text(1); anchor=query.text(2); payload=query.text(3);
        operation=query.text(4); original_position=query.number(5);
    }
    auto page=load_page(page_id);
    const auto dbs=databases();
    if (page.trashed || std::none_of(dbs.begin(),dbs.end(),[&](const auto& db){return db.id==page.database_id;}))
        throw std::runtime_error("Restore the page and its database before restoring these blocks");
    const auto fragment=decode_document(payload);
    auto locate=[&](const std::string& block_id) { return std::find_if(page.document.blocks.begin(),page.document.blocks.end(),[&](const auto& block){return block.id==block_id;}); };
    // A later sibling root may be live even if its children are still deleted.
    // Prefer it over the fallback live anchor so restoration order is irrelevant.
    {
        Statement siblings(database_.get(),"SELECT fragment FROM deleted_blocks WHERE operation_id=? AND parent_id=? AND original_position>? ORDER BY original_position");
        siblings.bind(1,operation).bind(2,parent).bind(3,original_position);
        while (siblings.row()) {
            const auto sibling=decode_document(siblings.text(0));
            const auto found=locate(sibling.blocks[0].id);
            if (found!=page.document.blocks.end() && found->parent_id==parent) { anchor=found->id; break; }
        }
    }
    for (std::size_t i=0;i<fragment.blocks.size();++i) {
        auto block=fragment.blocks[i];
        if (locate(block.id)!=page.document.blocks.end()) continue;
        if (i==0) block.parent_id=parent;
        auto position=page.document.blocks.size();
        if (!block.parent_id.empty()) {
            auto parent_block=locate(block.parent_id);
            if (parent_block==page.document.blocks.end()) {
                // A parent still waiting in this page's recovery must return first;
                // one that now lives elsewhere (or is gone) no longer anchors the fragment.
                Statement pending(database_.get(),"SELECT 1 FROM deleted_block_members m JOIN deleted_blocks f ON f.id=m.fragment_id WHERE m.block_id=? AND f.page_id=? AND NOT f.restored AND f.id<>?");
                pending.bind(1,block.parent_id).bind(2,page_id).bind(3,id);
                if (i || pending.row()) throw std::runtime_error("Restore the deleted parent block first");
                block.parent_id.clear(); if (reparented) *reparented=true;
            } else {
                std::unordered_set<std::string> descendants{parent_block->id};
                auto end=parent_block+1;
                while (end!=page.document.blocks.end() && descendants.count(end->parent_id)) { descendants.insert(end->id); ++end; }
                position=std::size_t(end-page.document.blocks.begin());
            }
        }
        std::string before=i==0?anchor:std::string{};
        if (i) for (auto j=i+1;j<fragment.blocks.size();++j)
            if (fragment.blocks[j].parent_id==block.parent_id && locate(fragment.blocks[j].id)!=page.document.blocks.end()) { before=fragment.blocks[j].id; break; }
        const auto next=locate(before);
        if (next!=page.document.blocks.end() && next->parent_id==block.parent_id) position=std::size_t(next-page.document.blocks.begin());
        page.document.blocks.insert(page.document.blocks.begin()+position,std::move(block));
    }
    validate(page.document); save_page(page); return page;
}
void Workspace::save_view(View& view)
{
    validate_view(view,properties(view.database_id));
    const auto id = view.id.empty() ? sql::id(database_.get()) : view.id;
    Statement add(database_.get(), "INSERT INTO saved_views(id,database_id,name,settings) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,settings=excluded.settings WHERE database_id=excluded.database_id");
    add.bind(1,id).bind(2,view.database_id).bind(3,view.name).bind(4,encode_view(view)).done(); sql::changed(database_.get());
    view.id = id;
}
void Workspace::delete_view(const std::string& id)
{
    Statement remove(database_.get(),"DELETE FROM saved_views WHERE id=?"); remove.bind(1,id).done(); sql::changed(database_.get());
}
std::vector<View> Workspace::views(const std::string& database) const
{
    Statement query(database_.get(), "SELECT id,name,settings FROM saved_views WHERE database_id=? ORDER BY name,id");
    query.bind(1,database); std::vector<View> result;
    while (query.row()) {
        View view; view.id=query.text(0); view.database_id=database; view.name=query.text(1);
        decode_view(view,query.text(2)); result.push_back(std::move(view));
    }
    return result;
}
std::vector<LinkEdge> Workspace::backlinks(ReferenceKind kind, const std::string& id) const
{
    if (id.empty()) return {};
    Statement query(database_.get(), "SELECT l.source_page,l.source_block,p.title,l.target_id,l.target_kind,l.run_index,b.position,b.payload FROM links l JOIN pages p ON p.id=l.source_page JOIN databases d ON d.id=p.database_id JOIN blocks b ON b.id=l.source_block WHERE NOT p.trashed AND NOT d.trashed AND ((l.target_kind=? AND l.target_id=?) OR (?=0 AND l.target_kind=1 AND l.target_id IN (SELECT id FROM blocks WHERE page_id=?))) ORDER BY p.title,b.position,l.run_index");
    query.bind(1,int(kind)).bind(2,id).bind(3,int(kind)).bind(4,id); std::vector<LinkEdge> result;
    while (query.row()) result.push_back(read_edge(query,*this));
    return result;
}
std::vector<LinkEdge> Workspace::outgoing_links(const std::string& id) const
{
    if (resolve_reference({ReferenceKind::Page,id}).state!=ReferenceState::Available) return {};
    Statement query(database_.get(),"SELECT l.source_page,l.source_block,p.title,l.target_id,l.target_kind,l.run_index,b.position,b.payload FROM links l JOIN blocks b ON b.id=l.source_block JOIN pages p ON p.id=l.source_page WHERE l.source_block IN (SELECT id FROM blocks WHERE page_id=?) ORDER BY b.position,l.run_index");
    query.bind(1,id); std::vector<LinkEdge> result;
    while (query.row()) result.push_back(read_edge(query,*this));
    const auto page=load_page(id);
    for (std::size_t position=0;position<page.document.blocks.size();++position) {
        const auto& block=page.document.blocks[position]; std::string context;
        for (const auto& run:block.runs) context+=run.reference && !run.reference->custom_label
            ?reference_label(*run.reference).value_or(run.text):run.text;
        for (std::size_t i=0;i<block.runs.size();++i) {
            const auto& run=block.runs[i]; if (!run.reference || !run.reference->target_id.empty()) continue;
            LinkEdge edge; edge.source_page=id; edge.source_block=block.id; edge.source_title=page.title;
            edge.kind=run.reference->kind; edge.label=run.text; edge.source_context=context;
            edge.unresolved_name=run.reference->unresolved_name; edge.database_scope=run.reference->database_scope;
            edge.position=std::int64_t(position); edge.run_index=std::int64_t(i); result.push_back(std::move(edge));
        }
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.position==b.position?a.run_index<b.run_index:a.position<b.position;});
    return result;
}
void Workspace::rebuild_links()
{
    sql::Transaction transaction(database_.get());
    sql::execute(database_.get(),R"(
        DROP TABLE IF EXISTS links;
        CREATE TABLE links(source_page TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
            source_block TEXT NOT NULL REFERENCES blocks(id) ON DELETE CASCADE,
            run_index INTEGER NOT NULL, target_kind INTEGER NOT NULL CHECK(target_kind IN(0,1)),
            target_id TEXT NOT NULL, PRIMARY KEY(source_block,run_index));
        CREATE INDEX links_target ON links(target_kind,target_id);
        CREATE INDEX links_source ON links(source_page);
    )");
    Statement pages(database_.get(),"SELECT id FROM pages");
    while (pages.row()) {
        const auto page = load_page(pages.text(0));
        for (const auto& block : page.document.blocks) storage_detail::insert_links(database_.get(),page.document.id,block);
    }
    transaction.commit();
}
} // namespace nebula
