#include "nebula/storage/workspace.hpp"
#include "nebula/storage/codec.hpp"
#include "sql.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {
using sql::Statement;
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
            if (!ref) continue;
            Statement edge(db, "INSERT INTO links(source_page,source_block,run_index,target_kind,target_id) VALUES(?,?,?,?,?)");
            edge.bind(1,page.id).bind(2,block.id).bind(3,std::int64_t(r)).bind(4,int(ref->kind)).bind(5,ref->target_id).done();
        }
    }
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
                                 const std::map<std::string,PropertyValue>& values)
{
    sql::Transaction transaction(database_.get());
    const auto page_id = sql::id(database_.get());
    PageRecord page{{page_id, {{sql::id(database_.get()), "", BlockKind::Paragraph, {}}}}, database, title, values, 1, false};
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
    Statement update(database_.get(), "UPDATE pages SET database_id=?,title=?,trashed=?,revision=revision+1,modified_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=? AND revision=?");
    update.bind(1,page.database_id).bind(2,page.title).bind(3,page.trashed).bind(4,page.document.id).bind(5,page.revision).done();
    sql::changed(database_.get());
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
    Statement query(database_.get(), "SELECT l.source_page,l.source_block,p.title,l.target_id,l.target_kind FROM links l JOIN pages p ON p.id=l.source_page JOIN databases d ON d.id=p.database_id WHERE NOT p.trashed AND NOT d.trashed AND ((l.target_kind=? AND l.target_id=?) OR (?=0 AND l.target_kind=1 AND l.target_id IN (SELECT id FROM blocks WHERE page_id=?))) ORDER BY p.title,l.source_block,l.run_index");
    query.bind(1,int(kind)).bind(2,id).bind(3,int(kind)).bind(4,id); std::vector<LinkEdge> result;
    while (query.row()) result.push_back({query.text(0),query.text(1),query.text(2),query.text(3),ReferenceKind(query.number(4))});
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
                if (!ref) continue;
                Statement edge(database_.get(),"INSERT INTO links(source_page,source_block,run_index,target_kind,target_id) VALUES(?,?,?,?,?)");
                edge.bind(1,page.document.id).bind(2,block.id).bind(3,std::int64_t(i)).bind(4,int(ref->kind)).bind(5,ref->target_id).done();
            }
    }
    transaction.commit();
}
} // namespace nebula
