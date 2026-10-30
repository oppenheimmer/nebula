#include "nebula/storage/workspace.hpp"
#include "nebula/format/codec.hpp"
#include "records_internal.hpp"
#include "sql.hpp"
#include <QString>
#include <QStringList>
#include <QChar>
#include <set>
#include <unordered_set>

namespace nebula {
namespace {
QString text(const std::string& value) {
    auto result=QString::fromUtf8(value.data(),qsizetype(value.size()));
    if (result.toUtf8().toStdString()!=value) throw std::invalid_argument("Search text must be valid UTF-8");
    result.replace(QChar::Null,' ');
    return result;
}
bool cjk(char32_t c) {
    return (c>=0x3040 && c<=0x30ff) || (c>=0x3100 && c<=0x312f) || (c>=0x31f0 && c<=0x31ff)
        || (c>=0x3400 && c<=0x9fff) || (c>=0x20000 && c<=0x323af)
        || (c>=0xac00 && c<=0xd7af) || (c>=0x1100 && c<=0x11ff);
}
QString normalized(const QString& value) {
    QString result;
    for (const auto point:value.normalized(QString::NormalizationForm_KC).toCaseFolded().toUcs4()) {
        const auto c=char32_t(point); if (cjk(c)) result+=' ';
        result+=QString::fromUcs4(&c,1); if (cjk(c)) result+=' ';
    }
    return result.simplified();
}
std::string expression(const std::string& query) {
    if (query.size()>4096) throw std::invalid_argument("Use at most 4096 UTF-8 bytes in a search");
    const auto terms=text(query).simplified().split(' ',Qt::SkipEmptyParts);
    if (terms.size()>32) throw std::invalid_argument("Use at most 32 search terms");
    QStringList quoted;
    for (auto term:terms) {
        const bool prefix=term.endsWith('*'); if (prefix) term.chop(1);
        term=normalized(term); bool searchable=false;
        for (const auto point:term.toUcs4()) if (QChar::isLetterOrNumber(char32_t(point))) searchable=true;
        if (!searchable) continue;
        term.replace('"',"\"\""); quoted.push_back('"'+term+'"'+(prefix?"*":""));
    }
    return quoted.join(" AND ").toStdString();
}
std::string property_text(const PropertyValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::monostate>) return {};
        else if constexpr (std::is_same_v<T,std::string>) return v;
        else if constexpr (std::is_same_v<T,double>) return QString::number(v,'g',17).toStdString();
        else if constexpr (std::is_same_v<T,bool>) return v?"true":"false";
        else if constexpr (std::is_same_v<T,Date>) return v.iso;
        else { std::string result; for (const auto& tag:v) { if (!result.empty()) result+=' '; result+=tag; } return result; }
    },value);
}
void index_page(sqlite3* database,const std::string& id) {
    const auto page=storage_detail::load_page(database,id);
    sql::Statement remove(database,"DELETE FROM page_search WHERE page_id=?"); remove.bind(1,id).done();
    std::string properties;
    sql::Statement fields(database,"SELECT d.name,v.value_json,d.kind,d.database_id FROM property_values v JOIN property_definitions d ON d.id=v.property_id WHERE v.page_id=? ORDER BY d.id"); fields.bind(1,id);
    while (fields.row()) {
        const auto value=decode_value(fields.text(1)); validate_value(PropertyKind(fields.number(2)),value);
        if (fields.text(3)!=page.database_id) throw std::runtime_error("Stored property belongs to another database");
        if (!std::holds_alternative<std::monostate>(value)) properties+=fields.text(0)+" "+property_text(value)+"\n";
    }
    auto insert=[&](const std::string& block,const std::string& body,const std::string& excerpt) {
        sql::Statement add(database,"INSERT INTO page_search(page_id,block_id,title,properties,body,excerpt) VALUES(?,?,?,?,?,?)");
        add.bind(1,id).bind(2,block).bind(3,normalized(text(page.title)).toStdString()).bind(4,normalized(text(properties)).toStdString())
            .bind(5,normalized(text(body)).toStdString()).bind(6,excerpt).done();
    };
    std::string whole;
    for (const auto& block:page.document.blocks) {
        std::string body;
        for (const auto& run:block.runs) {
            if (run.asset) body+=run.asset->label;
            else if (run.reference && !run.reference->custom_label) body+=storage_detail::reference_label(database,*run.reference).value_or(run.text);
            else body+=run.text;
        }
        if (body.empty()) body=block.asset_label;
        insert(block.id,body,body); if (!whole.empty()) whole+='\n'; whole+=body;
    }
    insert({},whole,page.title+"\n"+properties+whole);
}
} // namespace
void storage_detail::create_search(sqlite3* database) {
    sql::execute(database,"CREATE VIRTUAL TABLE page_search USING fts5(page_id UNINDEXED,block_id UNINDEXED,title,properties,body,excerpt UNINDEXED,tokenize='unicode61 remove_diacritics 2',prefix='2 3')");
}
void storage_detail::rebuild_search(sqlite3* database) {
    sql::execute(database,"DELETE FROM page_search"); sql::Statement pages(database,"SELECT id FROM pages ORDER BY id");
    while (pages.row()) index_page(database,pages.text(0));
}
void storage_detail::refresh_search(sqlite3* database,const std::string& id,const std::vector<std::string>& removed_blocks) {
    std::set<std::string> affected{id};
    sql::Statement referring(database,"SELECT DISTINCT source_page FROM links WHERE (target_kind=0 AND target_id=?) OR (target_kind=1 AND target_id IN(SELECT id FROM blocks WHERE page_id=?))");
    referring.bind(1,id).bind(2,id); while (referring.row()) affected.insert(referring.text(0));
    for (const auto& block:removed_blocks) {
        sql::Statement links(database,"SELECT DISTINCT source_page FROM links WHERE target_kind=1 AND target_id=?"); links.bind(1,block);
        while (links.row()) affected.insert(links.text(0));
    }
    for (const auto& page:affected) index_page(database,page);
}
std::vector<SearchResult> Workspace::search(const std::string& query,const std::string& database) const {
    const auto match=expression(query); if (match.empty()) return {};
    sql::Statement rows(database_.get(),"SELECT page_search.page_id,page_search.block_id,p.title,d.name,page_search.excerpt FROM page_search JOIN pages p ON p.id=page_search.page_id JOIN databases d ON d.id=p.database_id WHERE page_search MATCH ? AND NOT p.trashed AND NOT d.trashed AND (?='' OR p.database_id=?) ORDER BY bm25(page_search,0,0,2,1,4,0),p.title,page_search.block_id");
    rows.bind(1,match).bind(2,database).bind(3,database); std::vector<SearchResult> result; std::unordered_set<std::string> seen;
    while (rows.row()) {
        const auto id=rows.text(0); if (!seen.insert(id).second) continue;
        result.push_back({id,rows.text(1),rows.text(2),rows.text(3),rows.text(4)}); if (result.size()>=100) break;
    }
    return result;
}
void Workspace::rebuild_search() {
    sql::Transaction transaction(database_.get()); sql::execute(database_.get(),"DROP TABLE IF EXISTS page_search");
    storage_detail::create_search(database_.get()); storage_detail::rebuild_search(database_.get()); transaction.commit();
}
} // namespace nebula
