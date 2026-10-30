#include "nebula/storage/workspace.hpp"
#include "nebula/format/codec.hpp"
#include "sql.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {
using sql::Statement;
LinkEdge read_edge(const Statement& row) {
    LinkEdge edge; edge.source_page=row.text(0); edge.source_block=row.text(1);
    edge.source_title=row.text(2); edge.target_id=row.text(3); edge.kind=ReferenceKind(row.number(4));
    edge.run_index=row.number(5); edge.position=row.number(6);
    Block block; decode_block(block,row.text(7));
    for (const auto& run:block.runs) edge.source_context+=run.text;
    if (edge.source_context.empty()) edge.source_context=block.asset_label;
    if (edge.run_index>=0 && std::size_t(edge.run_index)<block.runs.size()) edge.label=block.runs[std::size_t(edge.run_index)].text;
    return edge;
}
void name_required(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos) throw std::invalid_argument("A nonempty name is required");
}
void insert_blocks(sqlite3* db, const Page& page) {
    for (std::size_t i = 0; i < page.blocks.size(); ++i) {
        const auto& block = page.blocks[i];
        Statement add(db, "INSERT INTO blocks(id,page_id,parent_id,position,kind,payload) VALUES(?,?,nullif(?,''),?,?,?)");
        add.bind(1,block.id).bind(2,page.id).bind(3,block.parent_id).bind(4,std::int64_t(i))
            .bind(5,int(block.kind)).bind(6,encode_block(block)).done();
        for (std::size_t r = 0; r < block.runs.size(); ++r) {
            const auto& ref = block.runs[r].reference;
            if (!ref || ref->target_id.empty()) continue;
            Statement edge(db, "INSERT INTO links(source_page,source_block,run_index,target_kind,target_id) VALUES(?,?,?,?,?)");
            edge.bind(1,page.id).bind(2,block.id).bind(3,std::int64_t(r)).bind(4,int(ref->kind)).bind(5,ref->target_id).done();
        }
    }
}
void archive_removed(sqlite3* db,const Page& previous,const Page& next) {
    std::unordered_set<std::string> live,removed;
    for (const auto& block:next.blocks) live.insert(block.id);
    for (const auto& block:previous.blocks) if (!live.count(block.id)) removed.insert(block.id);
    const auto operation=removed.empty()?std::string{}:sql::id(db);
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
        Statement add(db,"INSERT INTO deleted_blocks(id,page_id,parent_id,anchor_id,fragment,operation_id,original_position) VALUES(?,?,?,?,?,?,?)");
        add.bind(1,sql::id(db)).bind(2,previous.id).bind(3,root.parent_id).bind(4,anchor).bind(5,encode_document(fragment))
            .bind(6,operation).bind(7,std::int64_t(i)).done();
    }
    Statement groups(db,"SELECT id,fragment FROM deleted_blocks WHERE page_id=? AND NOT restored"); groups.bind(1,next.id);
    std::vector<std::string> completed;
    while (groups.row()) {
        const auto fragment=decode_document(groups.text(1));
        if (std::all_of(fragment.blocks.begin(),fragment.blocks.end(),[&](const auto& block){return live.count(block.id);})) {
            completed.push_back(groups.text(0));
        }
    }
    for (const auto& id:completed) { Statement done(db,"UPDATE deleted_blocks SET restored=1 WHERE id=?"); done.bind(1,id).done(); }
}
} // namespace
Database Workspace::create_database(const std::string& name)
{
    name_required(name);
    Database result{sql::id(database_.get()), name, false};
    Statement add(database_.get(), "INSERT INTO databases(id,name) VALUES(?,?)");
    add.bind(1,result.id).bind(2,name).done(); return result;
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
    const auto page_id = sql::id(database_.get());
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
    for (const auto& entry:values) {
        Statement add_value(database_.get(),"INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?)");
        add_value.bind(1,page_id).bind(2,entry.first).bind(3,encode_value(entry.second)).done();
    }
    transaction.commit(); return page;
}
PageRecord Workspace::load_page(const std::string& id) const
{
    Statement query(database_.get(), "SELECT database_id,title,revision,trashed FROM pages WHERE id=?");
    query.bind(1,id);
    if (!query.row()) throw std::runtime_error("Page not found");
    PageRecord page{{id,{}},query.text(0),query.text(1),{},query.number(2),bool(query.number(3))};
    Statement blocks(database_.get(), "SELECT id,parent_id,kind,payload FROM blocks WHERE page_id=? ORDER BY position");
    blocks.bind(1,id);
    while (blocks.row()) {
        Block block{blocks.text(0),blocks.text(1),BlockKind(blocks.number(2))};
        decode_block(block,blocks.text(3)); page.document.blocks.push_back(std::move(block));
    }
    validate(page.document);
    Statement values(database_.get(), "SELECT property_id,value_json FROM property_values WHERE page_id=?");
    values.bind(1,id);
    while (values.row()) page.properties.emplace(values.text(0),decode_value(values.text(1)));
    return page;
}
std::vector<PageRecord> Workspace::pages(const std::string& database, bool include_trashed) const
{
    Statement query(database_.get(), "SELECT p.id FROM pages p JOIN databases d ON d.id=p.database_id WHERE p.database_id=? AND (? OR (NOT p.trashed AND NOT d.trashed)) ORDER BY p.created_at,p.id");
    query.bind(1,database).bind(2,include_trashed); std::vector<PageRecord> result;
    while (query.row()) result.push_back(load_page(query.text(0)));
    return result;
}
void Workspace::save_page(PageRecord& page)
{
    validate(page.document);
    const auto definitions = properties(page.database_id);
    for (const auto& entry : page.properties) {
        const auto& id=entry.first;
        const auto& value=entry.second;
        const auto def = std::find_if(definitions.begin(),definitions.end(),[&](const auto& d) { return d.id == id; });
        if (def == definitions.end()) throw std::invalid_argument("Property does not belong to this database");
        validate_value(def->kind,value);
    }
    sql::Transaction transaction(database_.get());
    const auto previous=load_page(page.document.id);
    Statement update(database_.get(), "UPDATE pages SET database_id=?,title=?,trashed=?,revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND revision=?");
    update.bind(1,page.database_id).bind(2,page.title).bind(3,page.trashed).bind(4,page.document.id).bind(5,page.revision).done();
    sql::changed(database_.get());
    archive_removed(database_.get(),previous.document,page.document);
    Statement remove(database_.get(), "DELETE FROM blocks WHERE page_id=?"); remove.bind(1,page.document.id).done();
    insert_blocks(database_.get(),page.document);
    Statement clear(database_.get(), "DELETE FROM property_values WHERE page_id=?"); clear.bind(1,page.document.id).done();
    for (const auto& [id,value] : page.properties) {
        Statement add(database_.get(), "INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?)");
        add.bind(1,page.document.id).bind(2,id).bind(3,encode_value(value)).done();
    }
    transaction.commit(); ++page.revision;
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
void Workspace::move_page(PageRecord& page, const std::string& database)
{
    sql::Transaction transaction(database_.get());
    const auto previous=load_page(page.document.id);
    if (page.revision!=previous.revision) throw std::runtime_error("Page changed in another window; reopen it before moving");
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
    for (const auto& entry:values) {
        Statement add(database_.get(),"INSERT INTO property_values(page_id,property_id,value_json) VALUES(?,?,?)");
        add.bind(1,page.document.id).bind(2,entry.first).bind(3,encode_value(entry.second)).done();
    }
    auto next=previous; next.database_id=database; next.properties=std::move(values); ++next.revision;
    transaction.commit(); page=std::move(next);
}
void Workspace::replace_transfer_documents(const Page& source, std::int64_t source_revision,
                                           const Page& destination, std::int64_t destination_revision)
{
    validate(source); validate(destination);
    if (source.id==destination.id) throw std::invalid_argument("Choose a different destination page");
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
PageRecord Workspace::restore_deleted_blocks(const std::string& id)
{
    std::string page_id,parent,anchor,payload,operation;
    std::int64_t original_position=0;
    {
        Statement query(database_.get(),"SELECT page_id,parent_id,anchor_id,fragment,operation_id,original_position FROM deleted_blocks WHERE id=? AND NOT restored"); query.bind(1,id);
        if (!query.row()) throw std::runtime_error("Deleted blocks are already restored or unavailable");
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
            const auto parent_block=locate(block.parent_id);
            if (parent_block==page.document.blocks.end()) throw std::runtime_error("Restore the deleted parent block first");
            std::unordered_set<std::string> descendants{parent_block->id};
            auto end=parent_block+1;
            while (end!=page.document.blocks.end() && descendants.count(end->parent_id)) { descendants.insert(end->id); ++end; }
            position=std::size_t(end-page.document.blocks.begin());
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
    name_required(view.name);
    const auto defs = properties(view.database_id);
    const auto known = [&](const std::string& id) {
        return id.empty() || std::any_of(defs.begin(),defs.end(),[&](const auto& d) { return d.id == id; });
    };
    if (!known(view.sort_property)) throw std::invalid_argument("Unknown sort property");
    std::unordered_set<std::string> visible;
    for (const auto& id : view.visible_properties)
        if (id.empty() || !known(id) || !visible.insert(id).second) throw std::invalid_argument("Invalid visible columns");
    for (const auto& filter : view.filters) {
        if (!known(filter.property_id) || int(filter.comparison) < 0 || int(filter.comparison) > 5)
            throw std::invalid_argument("Invalid filter");
        auto kind=PropertyKind::Text;
        for (const auto& def:defs) if (def.id==filter.property_id) kind=def.kind;
        if (filter.comparison==Comparison::Contains) {
            if ((kind!=PropertyKind::Text && kind!=PropertyKind::Tags) || !std::holds_alternative<std::string>(filter.value))
                throw std::invalid_argument("Contains filters require text or tags and a text value");
        } else if (filter.comparison!=Comparison::Empty && filter.comparison!=Comparison::NotEmpty) {
            validate_value(kind,filter.value);
        }
    }
    const auto id = view.id.empty() ? sql::id(database_.get()) : view.id;
    Statement add(database_.get(), "INSERT INTO saved_views(id,database_id,name,settings) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,settings=excluded.settings WHERE database_id=excluded.database_id");
    add.bind(1,id).bind(2,view.database_id).bind(3,view.name).bind(4,encode_view(view)).done(); sql::changed(database_.get());
    view.id = id;
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
    while (query.row()) result.push_back(read_edge(query));
    return result;
}
std::vector<LinkEdge> Workspace::outgoing_links(const std::string& id) const
{
    if (resolve_reference({ReferenceKind::Page,id}).state!=ReferenceState::Available) return {};
    Statement query(database_.get(),"SELECT l.source_page,l.source_block,p.title,l.target_id,l.target_kind,l.run_index,b.position,b.payload FROM links l JOIN blocks b ON b.id=l.source_block JOIN pages p ON p.id=l.source_page WHERE l.source_block IN (SELECT id FROM blocks WHERE page_id=?) ORDER BY b.position,l.run_index");
    query.bind(1,id); std::vector<LinkEdge> result;
    while (query.row()) result.push_back(read_edge(query));
    const auto page=load_page(id);
    for (std::size_t position=0;position<page.document.blocks.size();++position) {
        const auto& block=page.document.blocks[position]; std::string context;
        for (const auto& run:block.runs) context+=run.text;
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
    sql::execute(database_.get(),"DELETE FROM links");
    Statement pages(database_.get(),"SELECT id FROM pages");
    while (pages.row()) {
        const auto page = load_page(pages.text(0));
        for (const auto& block : page.document.blocks)
            for (std::size_t i=0; i<block.runs.size(); ++i) {
                const auto& ref = block.runs[i].reference;
                if (!ref || ref->target_id.empty()) continue;
                Statement edge(database_.get(),"INSERT INTO links(source_page,source_block,run_index,target_kind,target_id) VALUES(?,?,?,?,?)");
                edge.bind(1,page.document.id).bind(2,block.id).bind(3,std::int64_t(i)).bind(4,int(ref->kind)).bind(5,ref->target_id).done();
            }
    }
    transaction.commit();
}
} // namespace nebula
