#include "nebula/format/codec.hpp"
#include "nebula/qt/utf8.hpp"
#include "nebula/storage/internals.hpp"
#include "nebula/storage/workspace.hpp"
#include "records_internal.hpp"
#include <QChar>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {
QString text(const std::string& value)
{
    auto result = utf8::checked(value, "Search text must be valid UTF-8");
    result.replace(QChar::Null, ' ');
    return result;
}
bool cjk(char32_t c)
{
    return (c >= 0x3040 && c <= 0x30ff) || (c >= 0x3100 && c <= 0x312f) || (c >= 0x31f0 && c <= 0x31ff) ||
           (c >= 0x3400 && c <= 0x9fff) || (c >= 0x20000 && c <= 0x323af) || (c >= 0xac00 && c <= 0xd7af) ||
           (c >= 0x1100 && c <= 0x11ff);
}
QString normalized(const QString& value)
{
    QString result;
    for (const auto point : value.normalized(QString::NormalizationForm_KC).toCaseFolded().toUcs4()) {
        const auto c = char32_t(point);
        if (cjk(c))
            result += ' ';
        result += QString::fromUcs4(&c, 1);
        if (cjk(c))
            result += ' ';
    }
    return result.simplified();
}
std::string folded(const std::string& value)
{
    return normalized(text(value)).toStdString();
}
// Each term is a quoted literal FTS5 phrase, optionally with a trailing prefix *.
std::vector<std::string> terms(const std::string& query)
{
    if (query.size() > 4096)
        throw std::invalid_argument("Use at most 4096 UTF-8 bytes in a search");
    const auto words = text(query).simplified().split(' ', Qt::SkipEmptyParts);
    if (words.size() > 32)
        throw std::invalid_argument("Use at most 32 search terms");
    std::vector<std::string> result;
    for (auto term : words) {
        const bool prefix = term.endsWith('*');
        if (prefix)
            term.chop(1);
        term = normalized(term);
        bool searchable = false;
        for (const auto point : term.toUcs4())
            if (QChar::isLetterOrNumber(char32_t(point)))
                searchable = true;
        if (!searchable)
            continue;
        term.replace('"', "\"\"");
        result.push_back(('"' + term + '"' + (prefix ? "*" : "")).toStdString());
    }
    return result;
}
std::string property_text(const PropertyValue& value)
{
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, std::string>)
                return v;
            else if constexpr (std::is_same_v<T, double>)
                return QString::number(v, 'g', 17).toStdString();
            else if constexpr (std::is_same_v<T, bool>)
                return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, Date>)
                return v.iso;
            else {
                std::string result;
                for (const auto& tag : v) {
                    if (!result.empty())
                        result += ' ';
                    result += tag;
                }
                return result;
            }
        },
        value);
}
using Labels = std::map<std::pair<int, std::string>, std::optional<std::string>>;
std::string block_body(sqlite3* database, const Block& block, Labels& labels)
{
    std::string body;
    for (const auto& run : block.runs) {
        if (run.asset)
            body += run.asset->label;
        else if (run.reference && !run.reference->custom_label) {
            const auto key = std::make_pair(int(run.reference->kind), run.reference->target_id);
            auto found = labels.find(key);
            if (found == labels.end())
                found = labels.emplace(key, storage_detail::reference_label(database, *run.reference)).first;
            body += found->second.value_or(run.text);
        } else
            body += run.text;
    }
    return body.empty() ? block.asset_label : body;
}
std::string properties_text(sqlite3* database, const PageRecord& page)
{
    std::string properties;
    sql::Statement fields(database,
                          "SELECT id,name,kind,database_id FROM property_definitions WHERE database_id=? ORDER BY id");
    fields.bind(1, page.database_id);
    while (fields.row()) {
        const auto value = page.properties.find(fields.text(0));
        if (value == page.properties.end())
            continue;
        validate_value(PropertyKind(fields.number(2)), value->second);
        if (!std::holds_alternative<std::monostate>(value->second))
            properties += fields.text(1) + " " + property_text(value->second) + "\n";
    }
    for (const auto& entry : page.properties) {
        sql::Statement owner(database, "SELECT database_id FROM property_definitions WHERE id=?");
        owner.bind(1, entry.first);
        if (!owner.row() || owner.text(0) != page.database_id)
            throw std::runtime_error("Stored property belongs to another database");
    }
    return properties;
}
void delete_row(sqlite3* database, const std::string& page, const std::string& block)
{
    sql::Statement find(database, "SELECT row FROM page_search_rows WHERE page_id=? AND block_id=?");
    find.bind(1, page).bind(2, block);
    if (!find.row())
        return;
    const auto row = find.number(0);
    sql::Statement remove(database, "DELETE FROM page_search WHERE rowid=?");
    remove.bind(1, row).done();
    sql::Statement unmap(database, "DELETE FROM page_search_rows WHERE row=?");
    unmap.bind(1, row).done();
}
void insert_row(sqlite3* database, const std::string& page, const std::string& block, const std::string& title,
                const std::string& properties, const std::string& body)
{
    sql::Statement add(database, "INSERT INTO page_search(page_id,block_id,title,properties,body) VALUES(?,?,?,?,?)");
    add.bind(1, page).bind(2, block).bind(3, folded(title)).bind(4, folded(properties)).bind(5, folded(body)).done();
    sql::Statement map(database, "INSERT INTO page_search_rows(row,page_id,block_id) VALUES(?,?,?)");
    map.bind(1, std::int64_t(sqlite3_last_insert_rowid(database))).bind(2, page).bind(3, block).done();
}
void index_page_row(sqlite3* database, const PageRecord& page)
{
    delete_row(database, page.document.id, {});
    insert_row(database, page.document.id, {}, page.title, properties_text(database, page), {});
}
void index_blocks(sqlite3* database, const PageRecord& page, const std::unordered_set<std::string>* only,
                  Labels& labels)
{
    for (const auto& block : page.document.blocks) {
        if (only && !only->count(block.id))
            continue;
        const auto body = block_body(database, block, labels);
        if (!body.empty())
            insert_row(database, page.document.id, block.id, {}, {}, body);
    }
}
std::string block_context(sqlite3* database, const std::string& id)
{
    sql::Statement query(database, "SELECT payload FROM blocks WHERE id=?");
    query.bind(1, id);
    if (!query.row())
        return {};
    Block block;
    decode_block(block, query.text(0));
    Labels labels;
    return block_body(database, block, labels);
}
std::string page_context(sqlite3* database, const std::string& id, const std::string& title)
{
    std::string context = title;
    sql::Statement query(database, "SELECT payload FROM blocks WHERE page_id=? ORDER BY position LIMIT 8");
    query.bind(1, id);
    Labels labels;
    while (query.row() && context.size() < 320) {
        Block block;
        decode_block(block, query.text(0));
        const auto body = block_body(database, block, labels);
        if (!body.empty())
            context += "\n" + body;
    }
    return context;
}
} // namespace

void storage_detail::create_search(sqlite3* database)
{
    sql::execute(
        database,
        "DROP TABLE IF EXISTS page_search; DROP TABLE IF EXISTS page_search_rows;"
        "CREATE VIRTUAL TABLE page_search USING fts5(page_id UNINDEXED,block_id UNINDEXED,title,properties,body,"
        "tokenize='unicode61 remove_diacritics 2',prefix='2 3');"
        "CREATE TABLE page_search_rows(row INTEGER PRIMARY KEY,page_id TEXT NOT NULL,block_id TEXT NOT "
        "NULL,UNIQUE(page_id,block_id))");
}
void storage_detail::rebuild_search(sqlite3* database)
{
    sql::execute(database, "DELETE FROM page_search; DELETE FROM page_search_rows");
    sql::Statement pages(database, "SELECT id FROM pages ORDER BY id");
    while (pages.row())
        index_page(database, load_page(database, pages.text(0)));
}
void WorkspaceInternals::rebuild_search(sqlite3* database)
{
    storage_detail::rebuild_search(database);
}
void storage_detail::remove_page_search(sqlite3* database, const std::string& page)
{
    sql::Statement rows(database, "SELECT row FROM page_search_rows WHERE page_id=?");
    rows.bind(1, page);
    std::vector<std::int64_t> list;
    while (rows.row())
        list.push_back(rows.number(0));
    for (const auto row : list) {
        sql::Statement remove(database, "DELETE FROM page_search WHERE rowid=?");
        remove.bind(1, row).done();
    }
    sql::Statement unmap(database, "DELETE FROM page_search_rows WHERE page_id=?");
    unmap.bind(1, page).done();
}
void storage_detail::index_page(sqlite3* database, const PageRecord& page)
{
    remove_page_search(database, page.document.id);
    Labels labels;
    index_page_row(database, page);
    index_blocks(database, page, nullptr, labels);
}
void storage_detail::reindex_referrers(sqlite3* database, const std::string& page, bool title,
                                       const std::vector<std::string>& blocks)
{
    std::set<std::string> affected;
    if (title) {
        sql::Statement referring(database,
                                 "SELECT DISTINCT source_page FROM links WHERE target_kind=0 AND target_id=?");
        referring.bind(1, page);
        while (referring.row())
            affected.insert(referring.text(0));
    }
    for (const auto& block : blocks) {
        sql::Statement referring(database,
                                 "SELECT DISTINCT source_page FROM links WHERE target_kind=1 AND target_id=?");
        referring.bind(1, block);
        while (referring.row())
            affected.insert(referring.text(0));
    }
    for (const auto& id : affected)
        index_page(database, load_page(database, id));
}
void storage_detail::refresh_search(sqlite3* database, const PageRecord& page, const SearchChange& change)
{
    for (const auto& id : change.removed)
        delete_row(database, page.document.id, id);
    std::unordered_set<std::string> changed(change.blocks.begin(), change.blocks.end());
    for (const auto& id : changed)
        delete_row(database, page.document.id, id);
    Labels labels;
    index_blocks(database, page, &changed, labels);
    if (change.page_row)
        index_page_row(database, page);
    if (change.label_title || !change.label_blocks.empty())
        reindex_referrers(database, page.document.id, change.label_title, change.label_blocks);
}
namespace {
QString fold(const std::string& value)
{
    return utf8::decode(value).normalized(QString::NormalizationForm_KC).toCaseFolded();
}
// Case-insensitive natural order ("Item 9" before "Item 10"), independent of the
// process locale so every machine shows the same order for the same workspace.
int natural_compare(const QString& a, const QString& b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            auto si = i, sj = j;
            while (si < a.size() && a[si] == '0')
                ++si;
            while (sj < b.size() && b[sj] == '0')
                ++sj;
            auto ei = si, ej = sj;
            while (ei < a.size() && a[ei].isDigit())
                ++ei;
            while (ej < b.size() && b[ej].isDigit())
                ++ej;
            if (ei - si != ej - sj)
                return ei - si < ej - sj ? -1 : 1;
            if (const int order = QStringView(a).mid(si, ei - si).compare(QStringView(b).mid(sj, ej - sj)))
                return order;
            i = ei;
            j = ej;
            while (i < a.size() && a[i].isDigit())
                ++i;
            while (j < b.size() && b[j].isDigit())
                ++j;
            continue;
        }
        if (a[i] != b[j])
            return a[i] < b[j] ? -1 : 1;
        ++i;
        ++j;
    }
    return i < a.size() ? 1 : j < b.size() ? -1 : 0;
}
} // namespace
TextRules storage_detail::text_rules()
{
    TextRules rules;
    rules.compare = [](const std::string& a, const std::string& b) { return natural_compare(fold(a), fold(b)); };
    rules.contains = [](const std::string& text, const std::string& needle) {
        return fold(text).contains(fold(needle));
    };
    return rules;
}
std::vector<SearchResult> Workspace::search(const std::string& query, const std::string& database,
                                            const std::vector<LinkFilter>& filters) const
{
    validate_link_filters(filters);
    const auto list = terms(query);
    if (list.empty() && filters.empty())
        return {};
    sqlite3* db = database_.get();
    std::vector<SearchResult> result;
    if (list.empty()) {
        const auto command = "SELECT p.id,'',p.title,d.name FROM pages p JOIN databases d ON d.id=p.database_id WHERE "
                             "NOT p.trashed AND NOT d.trashed AND (?='' OR p.database_id=?)" +
                             storage_detail::link_predicate(filters) + " ORDER BY p.title,p.id LIMIT 100";
        sql::Statement rows(db, command);
        rows.bind(1, database).bind(2, database);
        storage_detail::bind_link_filters(rows, 3, filters);
        while (rows.row())
            result.push_back({rows.text(0), {}, rows.text(2), rows.text(3), rows.text(2)});
        return result;
    }
    struct Match {
        std::string block, title, database;
        double score = 0;
        bool whole = false;
        std::size_t terms = 0;
    };
    std::unordered_map<std::string, Match> matches;
    const auto command =
        "SELECT page_search.page_id,page_search.block_id,p.title,d.name,bm25(page_search,0,0,2,1,4) FROM page_search "
        "JOIN pages p ON p.id=page_search.page_id JOIN databases d ON d.id=p.database_id WHERE page_search MATCH ? AND "
        "NOT p.trashed AND NOT d.trashed AND (?='' OR p.database_id=?)" +
        storage_detail::link_predicate(filters);
    // Rows matching every term in one block or in the page row.
    {
        std::string all;
        for (const auto& term : list) {
            if (!all.empty())
                all += " AND ";
            all += term;
        }
        sql::Statement rows(db, command);
        rows.bind(1, all).bind(2, database).bind(3, database);
        storage_detail::bind_link_filters(rows, 4, filters);
        while (rows.row()) {
            const auto score = rows.real(4);
            auto& match = matches[rows.text(0)];
            if (!match.whole || score < match.score) {
                match.block = rows.text(1);
                match.title = rows.text(2);
                match.database = rows.text(3);
                match.score = score;
                match.whole = true;
            }
        }
    }
    // Pages whose terms occur in different blocks or in the title/properties. Rows
    // matching every term rank first, so skip this pass once they fill the results.
    if (list.size() > 1 && matches.size() < 100) {
        std::unordered_map<std::string, Match> spread;
        for (const auto& term : list) {
            std::unordered_map<std::string, double> best;
            sql::Statement rows(db, command);
            rows.bind(1, term).bind(2, database).bind(3, database);
            storage_detail::bind_link_filters(rows, 4, filters);
            while (rows.row()) {
                const auto page = rows.text(0);
                const auto score = rows.real(4);
                auto found = best.find(page);
                if (found == best.end() || score < found->second)
                    best[page] = score;
                auto& match = spread[page];
                match.title = rows.text(2);
                match.database = rows.text(3);
            }
            for (const auto& entry : best) {
                auto& match = spread[entry.first];
                match.score += entry.second;
                ++match.terms;
            }
        }
        for (auto& entry : spread)
            if (entry.second.terms == list.size() && !matches.count(entry.first))
                matches.emplace(entry.first, entry.second);
    }
    std::vector<std::pair<std::string, Match>> ordered(matches.begin(), matches.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.second.score != b.second.score)
            return a.second.score < b.second.score;
        if (a.second.title != b.second.title)
            return a.second.title < b.second.title;
        return a.first < b.first;
    });
    for (const auto& entry : ordered) {
        if (result.size() >= 100)
            break;
        const auto& match = entry.second;
        result.push_back(
            {entry.first, match.block, match.title, match.database,
             match.block.empty() ? page_context(db, entry.first, match.title) : block_context(db, match.block)});
    }
    return result;
}
void Workspace::rebuild_search()
{
    sql::Transaction transaction(database_.get());
    storage_detail::create_search(database_.get());
    storage_detail::rebuild_search(database_.get());
    transaction.commit();
}
} // namespace nebula
