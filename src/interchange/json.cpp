#include "internal.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <set>

namespace nebula::interchange {
namespace {
using namespace detail;
// Imported revisions stay far below int64 overflow so later saves can increment them.
constexpr std::int64_t RevisionLimit = std::int64_t(1) << 62;
struct Field {
    const char* name;
    char type;
}; // s=text, n=nullable text, i=decimal int64, b=bool, j=JSON object.
struct Table {
    const char* name;
    std::vector<Field> fields;
};
const std::vector<Table> tables{
    {"workspace_meta", {{"id", 'i'}, {"workspace_id", 's'}}},
    {"databases", {{"id", 's'}, {"name", 's'}, {"trashed", 'b'}}},
    {"pages",
     {{"id", 's'},
      {"database_id", 's'},
      {"title", 's'},
      {"revision", 'i'},
      {"trashed", 'b'},
      {"created_at", 's'},
      {"modified_at", 's'}}},
    {"blocks", {{"id", 's'}, {"page_id", 's'}, {"parent_id", 'n'}, {"position", 'i'}, {"kind", 'i'}, {"payload", 'j'}}},
    {"property_definitions", {{"id", 's'}, {"database_id", 's'}, {"name", 's'}, {"kind", 'i'}}},
    {"property_values", {{"page_id", 's'}, {"property_id", 's'}, {"value_json", 'j'}}},
    {"saved_views", {{"id", 's'}, {"database_id", 's'}, {"name", 's'}, {"settings", 'j'}}},
    {"deleted_blocks",
     {{"id", 's'},
      {"page_id", 's'},
      {"parent_id", 's'},
      {"anchor_id", 's'},
      {"fragment", 'j'},
      {"restored", 'b'},
      {"removed_at", 's'},
      {"operation_id", 's'},
      {"original_position", 'i'}}},
    {"attachments", {{"id", 's'}, {"name", 's'}, {"size", 'i'}, {"sha256", 's'}, {"image", 'b'}}}};
void keys(const QJsonObject& object, const QStringList& allowed, bool exact = false)
{
    for (auto i = object.begin(); i != object.end(); ++i)
        if (!allowed.contains(i.key()))
            throw std::runtime_error("Unsupported JSON field: " + s(i.key()));
    if (exact && object.size() != allowed.size())
        throw std::runtime_error("Required JSON fields are missing");
}
QJsonObject parse(const std::string& text)
{
    q(text);
    QJsonParseError error;
    const auto bytes = QByteArray::fromStdString(text);
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("Invalid interchange JSON object");
    // Qt accepts duplicate names with last-value semantics. Reject them before
    // accepting any canonical field, including nested block/property payloads.
    std::vector<std::set<QString>> objects;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '{')
            objects.emplace_back();
        else if (bytes[i] == '}')
            objects.pop_back();
        else if (bytes[i] == '"') {
            const auto first = i++;
            while (i < bytes.size() && bytes[i] != '"') {
                if (bytes[i] == '\\')
                    ++i;
                ++i;
            }
            auto next = i + 1;
            while (next < bytes.size() &&
                   (bytes[next] == ' ' || bytes[next] == '\n' || bytes[next] == '\r' || bytes[next] == '\t'))
                ++next;
            if (next < bytes.size() && bytes[next] == ':') {
                const auto name =
                    QJsonDocument::fromJson("[" + bytes.mid(first, i - first + 1) + "]").array()[0].toString();
                if (objects.empty() || !objects.back().insert(name).second)
                    throw std::runtime_error("Duplicate JSON field: " + s(name));
            }
        }
    }
    return document.object();
}
std::string serialized(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}
void block_keys(const QJsonObject& block)
{
    auto allowed = QStringList{"version", "runs", "asset_label", "checked"};
    if (block["version"].toInt() >= 3)
        allowed.push_back("attachment_id");
    keys(block, allowed);
    for (const auto& entry : block["runs"].toArray()) {
        const auto run = entry.toObject();
        keys(run, {"text", "bold", "italic", "reference", "asset"});
        if (run.contains("reference")) {
            auto fields = QStringList{"kind", "target", "unresolved_name", "scope"};
            if (block["version"].toInt() >= 2)
                fields.push_back("custom_label");
            keys(run["reference"].toObject(), fields);
        }
        if (run.contains("asset")) {
            auto fields = QStringList{"image", "label"};
            if (block["version"].toInt() >= 3)
                fields.push_back("attachment_id");
            keys(run["asset"].toObject(), fields);
        }
    }
}
void value_keys(const QJsonObject& value)
{
    keys(value, value["type"].toInt() == 0 ? QStringList{"version", "type"} : QStringList{"version", "type", "value"});
}
void payload(const QString& name, const QJsonObject& object)
{
    const auto text = serialized(object);
    if (name == "payload") {
        Block block;
        decode_block(block, text);
        block_keys(object);
    } else if (name == "value_json") {
        decode_value(text);
        value_keys(object);
    } else if (name == "settings") {
        View view;
        decode_view(view, text);
        auto allowed = QStringList{"version", "visible", "filters", "sort", "ascending"};
        if (object["version"].toInt() >= 2)
            allowed.push_back("link_filters");
        keys(object, allowed);
        for (const auto& entry : object["filters"].toArray()) {
            const auto filter = entry.toObject();
            keys(filter, {"property", "comparison", "value"}, true);
            value_keys(filter["value"].toObject());
        }
        for (const auto& entry : object["link_filters"].toArray())
            keys(entry.toObject(), {"relation", "page"}, true);
    } else if (name == "fragment") {
        decode_document(text);
        keys(object, {"version", "id", "blocks"}, true);
        for (const auto& entry : object["blocks"].toArray()) {
            const auto block = entry.toObject();
            keys(block, {"id", "parent", "kind", "payload"}, true);
            block_keys(block["payload"].toObject());
        }
    }
}
std::int64_t integer(const QJsonValue& value)
{
    if (!value.isString())
        throw std::runtime_error("Expected a decimal int64 string");
    bool okay = false;
    const auto number = value.toString().toLongLong(&okay);
    if (!okay || QString::number(number) != value.toString())
        throw std::runtime_error("Invalid decimal int64 string");
    return number;
}
QString columns(const Table& table)
{
    QStringList fields;
    for (const auto& field : table.fields)
        fields.push_back(field.name);
    return fields.join(',');
}
std::string text_field(const QString& name, const QString& value)
{
    const auto text = s(value);
    if (name == "id" || name.endsWith("_id")) {
        if (text.find('\0') != std::string::npos || (text.empty() && name != "parent_id" && name != "anchor_id"))
            throw std::runtime_error("Identities must be nonempty UTF-8 without NUL");
    }
    return text;
}
QJsonObject records(sqlite3* db)
{
    QJsonObject result;
    for (const auto& table : tables) {
        const auto query = "SELECT " + columns(table) + " FROM " + table.name + " ORDER BY rowid";
        sql::Statement rows(db, s(query).c_str());
        QJsonArray list;
        while (rows.row()) {
            QJsonObject row;
            int column = 0;
            for (const auto& field : table.fields) {
                const auto type = rows.type(column);
                QJsonValue value;
                if (field.type == 'i' || field.type == 'b') {
                    if (type != SQLITE_INTEGER)
                        throw std::runtime_error("Stored integer has an unsupported SQLite type");
                    value = field.type == 'i' ? QJsonValue(QString::number(rows.number(column)))
                                              : QJsonValue(bool(rows.number(column)));
                    if (field.type == 'b' && rows.number(column) != 0 && rows.number(column) != 1)
                        throw std::runtime_error("Invalid stored boolean");
                } else if (field.type == 'n' && type == SQLITE_NULL)
                    value = QJsonValue::Null;
                else {
                    if (type != SQLITE_TEXT)
                        throw std::runtime_error("Stored text has an unsupported SQLite type");
                    const auto text = rows.text(column);
                    if (field.type == 'j') {
                        const auto object = parse(text);
                        payload(field.name, object);
                        value = object;
                    } else
                        value = q(text_field(field.name, q(text)));
                }
                row[field.name] = value;
                ++column;
            }
            list.append(row);
        }
        result[table.name] = list;
    }
    return result;
}
void validate_records(const Workspace& workspace, sqlite3* db)
{
    sql::Statement relationships(db, "PRAGMA foreign_key_check");
    if (relationships.row())
        throw std::runtime_error("Invalid imported relationships");
    // SQLite 3.46.1 can retain a stale FTS5 integrity cursor on a read-only
    // connection after another connection commits. A normal virtual-table read
    // refreshes it within this snapshot; do not bypass the integrity check.
    if (sql::scalar(db, "SELECT count(*) FROM sqlite_schema WHERE name='page_search'") != "0") {
        sql::Statement refresh(db, "SELECT rowid FROM page_search LIMIT 1");
        refresh.row();
    }
    const auto integrity = sql::scalar(db, "PRAGMA quick_check");
    if (integrity != "ok")
        throw std::runtime_error("Invalid workspace integrity: " + integrity);
    sql::Statement meta(db, "SELECT id,workspace_id FROM workspace_meta");
    if (!meta.row() || meta.number(0) != 1 || meta.text(1).empty() || meta.text(1).find('\0') != std::string::npos ||
        meta.row())
        throw std::runtime_error("Expected one permanent workspace identity");
    sql::Statement positions(
        db, "SELECT page_id FROM blocks GROUP BY page_id HAVING min(position)<>0 OR max(position)<>count(*)-1");
    if (positions.row())
        throw std::runtime_error("Block positions must be consecutive from zero");
    for (const auto& database : workspace.databases(true)) {
        const auto definitions = workspace.properties(database.id);
        for (const auto& page : workspace.pages(database.id, true)) {
            for (const auto& entry : page.properties) {
                const auto& id = entry.first;
                const auto& value = entry.second;
                const auto found =
                    std::find_if(definitions.begin(), definitions.end(), [&](const auto& def) { return def.id == id; });
                if (found == definitions.end())
                    throw std::runtime_error("Property value belongs to another database");
                validate_value(found->kind, value);
            }
        }
        for (const auto& view : workspace.views(database.id)) {
            try {
                validate_view(view, definitions);
            } catch (const std::invalid_argument& error) {
                throw std::runtime_error(std::string("Invalid saved view: ") + error.what());
            }
        }
    }
    sql::Statement fragments(db, "SELECT page_id,fragment,original_position FROM deleted_blocks");
    while (fragments.row())
        if (decode_document(fragments.text(1)).id != fragments.text(0) || fragments.number(2) < 0)
            throw std::runtime_error("Invalid deleted-block recovery record");
    sql::Statement dates(db, "SELECT created_at FROM pages UNION ALL SELECT modified_at FROM pages UNION ALL SELECT "
                             "removed_at FROM deleted_blocks");
    while (dates.row()) {
        const auto date = q(dates.text(0));
        if (!date.endsWith('Z') || !QDateTime::fromString(date, Qt::ISODateWithMs).isValid())
            throw std::runtime_error("Invalid UTC timestamp");
    }
}
fs::path relative_file(const QString& name)
{
    const auto value = s(name);
    const auto relative = fs::u8path(value);
    if (value.find('\0') != std::string::npos || value.find('\\') != std::string::npos || relative.is_absolute() ||
        relative.empty() || *relative.begin() != "attachments")
        throw std::runtime_error("Invalid JSON attachment path");
    std::size_t components = 0;
    for (const auto& part : relative) {
        if (part.empty() || part == "." || part == "..")
            throw std::runtime_error("Unsafe JSON attachment path");
        ++components;
    }
    if (components < 2 || relative.generic_u8string() != value)
        throw std::runtime_error("Noncanonical JSON attachment path");
    return relative;
}
std::set<std::string> file_list(const fs::path& root)
{
    if (fs::is_symlink(root / "attachments") || !fs::is_directory(root / "attachments"))
        throw std::runtime_error("Missing regular attachment folder");
    std::set<std::string> result;
    for (const auto& entry : fs::recursive_directory_iterator(root / "attachments")) {
        if (entry.is_symlink())
            throw std::runtime_error("Attachment symlinks cannot be interchanged");
        if (entry.is_directory())
            continue;
        if (!entry.is_regular_file())
            throw std::runtime_error("Unsupported attachment file");
        result.insert(entry.path().lexically_relative(root).generic_u8string());
    }
    return result;
}
} // namespace
void export_json(const Workspace& workspace, const std::filesystem::path& destination)
{
    sqlite3* database = WorkspaceInternals::connection(workspace);
    const auto& directory = workspace.directory();
    if (WorkspaceInternals::schema_version(database) != WorkspaceInternals::current_schema())
        throw std::runtime_error("Open and upgrade the workspace before JSON export");
    reject_managed_destination(directory, destination);
    files::WorkspaceLock lock(directory, false);
    Snapshot snapshot(database);
    validate_records(workspace, database);
    const auto data = records(database);
    for (const auto& issue : WorkspaceInternals::audit_unlocked(workspace))
        if (issue.prevents_backup)
            throw std::runtime_error("Incomplete managed files: " + issue.message);
    NewDirectory output(destination);
    files::make_private_directory(destination / "attachments");
    QJsonArray files;
    for (const auto& name : file_list(directory)) {
        const auto relative = relative_file(q(name));
        fs::create_directories((destination / relative).parent_path());
        const auto digest = files::copy_beneath(directory, relative, destination / relative);
        files.append(
            QJsonObject{{"path", q(name)}, {"size", QString::number(digest.size)}, {"sha256", q(digest.sha256)}});
    }
    const QJsonObject object{{"format", "nebula-workspace"},
                             {"version", 1},
                             {"schema", WorkspaceInternals::interchange_contract()},
                             {"tables", data},
                             {"files", files}};
    const auto content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (content.size() > 64 * 1024 * 1024)
        throw std::runtime_error("JSON manifest exceeds the supported 64 MiB limit");
    files::write_new(destination / "workspace.json", content.toStdString());
    std::vector<fs::path> directories{destination / "attachments"};
    for (const auto& entry : fs::recursive_directory_iterator(destination / "attachments"))
        if (entry.is_directory())
            directories.push_back(entry.path());
    for (auto i = directories.rbegin(); i != directories.rend(); ++i)
        files::sync_directory(*i);
    output.complete();
}
Workspace import_json(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    if (!source.is_absolute() || fs::is_symlink(source) || !fs::is_directory(source))
        throw std::invalid_argument("Choose an existing JSON export folder");
    reject_managed_destination(fs::weakly_canonical(source), destination);
    const auto object = parse(read(source / "workspace.json"));
    keys(object, {"format", "version", "schema", "tables", "files"}, true);
    if (object["format"] != "nebula-workspace" || object["version"] != 1 ||
        (object["schema"] != 8 && object["schema"] != 9))
        throw std::runtime_error("Unsupported JSON interchange version/schema");
    if (!object["tables"].isObject() || !object["files"].isArray())
        throw std::runtime_error("Malformed JSON interchange collections");
    const auto data = object["tables"].toObject();
    QStringList names;
    for (const auto& table : tables)
        names.push_back(table.name);
    keys(data, names, true);
    std::set<std::string> expected;
    for (const auto& entry : object["files"].toArray()) {
        if (!entry.isObject())
            throw std::runtime_error("Invalid file entry");
        const auto file = entry.toObject();
        keys(file, {"path", "size", "sha256"}, true);
        if (!file["path"].isString() || !file["sha256"].isString() || integer(file["size"]) < 0)
            throw std::runtime_error("Invalid file metadata");
        const auto name = file["path"].toString();
        relative_file(name);
        if (!expected.insert(s(name)).second)
            throw std::runtime_error("Duplicate JSON file path");
    }
    if (file_list(source) != expected)
        throw std::runtime_error("JSON attachment file list does not match");
    NewDirectory output(destination);
    auto workspace = Workspace::open(destination);
    {
        sqlite3* connection = WorkspaceInternals::connection(workspace);
        files::WorkspaceLock lock(workspace.directory(), true);
        sql::Transaction transaction(connection);
        sql::execute(connection, "DELETE FROM workspace_meta");
        for (const auto& table : tables) {
            const auto list = data[table.name];
            if (!list.isArray())
                throw std::runtime_error("Expected canonical table array");
            QStringList placeholders;
            for (std::size_t i = 0; i < table.fields.size(); ++i)
                placeholders.push_back("?");
            const auto command = "INSERT INTO " + QString(table.name) + "(" + columns(table) + ") VALUES(" +
                                 placeholders.join(',') + ")";
            for (const auto& entry : list.toArray()) {
                if (!entry.isObject())
                    throw std::runtime_error("Expected canonical row object");
                const auto row = entry.toObject();
                QStringList fields;
                for (const auto& field : table.fields)
                    fields.push_back(field.name);
                keys(row, fields, true);
                sql::Statement add(connection, s(command).c_str());
                int column = 1;
                for (const auto& field : table.fields) {
                    const auto value = row[field.name];
                    if (field.type == 'i') {
                        const auto number = integer(value);
                        if (QString(table.name) == "pages" && QString(field.name) == "revision" &&
                            (number < 1 || number >= RevisionLimit))
                            throw std::runtime_error("Page revisions must be between 1 and 2^62");
                        add.bind(column, number);
                    } else if (field.type == 'b') {
                        if (!value.isBool())
                            throw std::runtime_error("Expected boolean");
                        add.bind(column, value.toBool());
                    } else if (field.type == 'j') {
                        if (!value.isObject())
                            throw std::runtime_error("Expected payload object");
                        if (object["schema"] == 8 && QString(field.name) == "settings" &&
                            value.toObject()["version"].toInt() > 1)
                            throw std::runtime_error("View payload is newer than its declared schema");
                        payload(field.name, value.toObject());
                        add.bind(column, serialized(value.toObject()));
                    } else if (field.type == 'n' && value.isNull())
                        add.bind_null(column);
                    else {
                        if (!value.isString())
                            throw std::runtime_error("Expected UTF-8 text");
                        add.bind(column, text_field(field.name, value.toString()));
                    }
                    ++column;
                }
                add.done();
            }
        }
        WorkspaceInternals::rebuild_recovery_members(connection);
        validate_records(workspace, connection);
        for (const auto& entry : object["files"].toArray()) {
            const auto file = entry.toObject();
            const auto relative = relative_file(file["path"].toString());
            fs::create_directories((destination / relative).parent_path());
            const auto digest = files::copy_beneath(source, relative, destination / relative);
            if (digest.size != integer(file["size"]) || q(digest.sha256) != file["sha256"])
                throw std::runtime_error("JSON attachment checksum mismatch");
        }
        for (const auto& issue : WorkspaceInternals::audit_unlocked(workspace))
            if (issue.prevents_backup)
                throw std::runtime_error("Incomplete imported attachment: " + issue.message);
        files::sync_directory(destination / "attachments");
        transaction.commit();
    }
    WorkspaceInternals::set_identity(workspace, sql::scalar(WorkspaceInternals::connection(workspace),
                                                            "SELECT workspace_id FROM workspace_meta WHERE id=1"));
    workspace.rebuild_links();
    workspace.rebuild_search();
    std::vector<fs::path> directories{destination / "attachments"};
    for (const auto& entry : fs::recursive_directory_iterator(destination / "attachments"))
        if (entry.is_directory())
            directories.push_back(entry.path());
    for (auto i = directories.rbegin(); i != directories.rend(); ++i)
        files::sync_directory(*i);
    output.complete();
    return workspace;
}
} // namespace nebula::interchange
