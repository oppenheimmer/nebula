#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include "nebula/storage/workspace.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <sqlite3.h>
using namespace nebula;
namespace {
std::filesystem::path path(const QString& value)
{
    return std::filesystem::u8path(value.toUtf8().constData());
}
void put(const QString& file, const QByteArray& bytes)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
        throw std::runtime_error("test write failed");
}
QByteArray read(const QString& file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        throw std::runtime_error("test read failed");
    return in.readAll();
}
std::string text(const Block& block)
{
    std::string result;
    for (const auto& run : block.runs)
        result += run.text;
    return result;
}
QJsonObject object(const QString& file)
{
    return QJsonDocument::fromJson(read(file)).object();
}
void put(const QString& file, const QJsonObject& value)
{
    put(file, QJsonDocument(value).toJson());
}
void sql(const QString& file, const char* command)
{
    sqlite3* raw = nullptr;
    if (sqlite3_open(file.toUtf8().constData(), &raw) != SQLITE_OK)
        throw std::runtime_error("test database open failed");
    std::unique_ptr<sqlite3, DatabaseCloser> connection(raw);
    if (sqlite3_exec(raw, command, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(raw));
}
} // namespace
class InterchangeTests : public QObject {
    Q_OBJECT
private slots:
    void markdown_subset_remaps_pages_blocks_and_files();
    void markdown_reports_losses_and_never_fetches_resources();
    void markdown_rejection_keeps_existing_data_and_files();
    void markdown_forward_missing_and_duplicate_anchors();
    void markdown_sql_failure_and_missing_export_file_leave_no_partial_data();
    void markdown_image_inline_asset_and_unsafe_identity_roundtrip();
    void json_roundtrip_preserves_the_full_model_and_retained_files();
    void json_rejects_unsupported_malformed_and_colliding_rows();
    void json_rejects_missing_changed_unsafe_and_unlisted_files();
    void json_rejects_duplicate_keys_including_escaped_names();
    void json_preserves_reference_filters_and_reads_schema_eight_exports();
};
void InterchangeTests::markdown_subset_remaps_pages_blocks_and_files()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    auto target = w.create_page(db.id, "Duplicate", {}, {{"heading", {}, BlockKind::Heading, {{u8"日本語 café"}}}});
    auto source = w.create_page(db.id, "Duplicate", {},
                                {{"paragraph",
                                  {},
                                  BlockKind::Paragraph,
                                  {{"Bold", true},
                                   {" italic", false, true},
                                   {u8" a\\b * [] <>\u2028next "},
                                   {"jump", false, false, Reference{ReferenceKind::Block, "heading"}}}},
                                 {"bullet", {}, BlockKind::Bullet, {{"one"}}},
                                 {"todo", {}, BlockKind::Todo, {{"two"}}, {}, true},
                                 {"divider", {}, BlockKind::Divider}});
    put(folder.filePath("asset.txt"), "attachment bytes");
    const auto imported = w.import_attachment(source, "divider", path(folder.filePath("asset.txt")), false);
    auto report = interchange::export_markdown(w, path(folder.filePath("markdown")));
    QCOMPARE(report.pages.size(), std::size_t(2));
    QVERIFY(!report.warnings.empty());
    const auto targetfile = folder.filePath("markdown/id-" + QString::fromStdString(target.document.id) + ".md");
    QVERIFY(read(targetfile).contains("block-heading"));
    QVERIFY(QFile::remove(folder.filePath("asset.txt")));
    const auto count = w.pages(db.id).size();
    report = interchange::import_markdown(w, path(folder.filePath("markdown")), db.id);
    QCOMPARE(report.pages.size(), std::size_t(2));
    QCOMPARE(w.pages(db.id).size(), count + 2);
    QVERIFY(!report.warnings.empty());
    PageRecord first, second;
    for (const auto& id : report.pages) {
        auto p = w.load_page(id);
        if (p.document.blocks[0].kind == BlockKind::Heading)
            second = p;
        else
            first = p;
    }
    QVERIFY(!first.document.id.empty());
    QVERIFY(!second.document.id.empty());
    QVERIFY(first.document.id != source.document.id);
    QCOMPARE(text(first.document.blocks[0]), text(source.document.blocks[0]));
    QVERIFY(first.document.blocks[0].runs[0].bold);
    QCOMPARE(first.document.blocks[0].runs.back().reference->target_id, second.document.blocks[0].id);
    QCOMPARE(first.document.blocks[1].kind, BlockKind::Bullet);
    QCOMPARE(first.document.blocks[2].kind, BlockKind::Todo);
    QVERIFY(first.document.blocks[2].checked);
    QCOMPARE(first.document.blocks[3].kind, BlockKind::Divider);
    const auto file = first.document.blocks[4].attachment_id;
    QVERIFY(!file.empty());
    QVERIFY(file != imported.file.id);
    QCOMPARE(read(QString::fromStdString(w.attachment_path(file).u8string())), QByteArray("attachment bytes"));
    QCOMPARE(w.backlinks(ReferenceKind::Page, second.document.id).size(), std::size_t(1));
    QCOMPARE(w.search("cafe").size(), std::size_t(2));
    QCOMPARE(w.load_page(source.document.id).revision, source.revision);
}
void InterchangeTests::markdown_reports_losses_and_never_fetches_resources()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkdir("input"));
    put(folder.filePath("input/one.md"),
        "# Title\n\n## Heading\n- one\n- [x] two\n\n![remote](https://invalid.example/image.png) "
        "[web](https://invalid.example)\n\n> quote\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto report = interchange::import_markdown(w, path(folder.filePath("input")), db.id);
    QVERIFY(report.warnings.size() >= 2);
    QVERIFY(w.attachments().empty());
    const auto page = w.load_page(report.pages[0]);
    QCOMPARE(page.document.blocks[0].kind, BlockKind::Heading);
    QCOMPARE(page.document.blocks[1].kind, BlockKind::Bullet);
    QCOMPARE(page.document.blocks[2].kind, BlockKind::Todo);
    QCOMPARE(text(page.document.blocks[3]), std::string("remote web"));
    QVERIFY(w.outgoing_links(page.document.id).empty());
    auto nested = page;
    nested.document.blocks[1].parent_id = nested.document.blocks[0].id;
    w.save_page(nested);
    const auto exported = interchange::export_markdown(w, path(folder.filePath("output")));
    bool nesting = false;
    for (const auto& warning : exported.warnings)
        if (warning.find("nesting") != std::string::npos)
            nesting = true;
    QVERIFY(nesting);
    QVERIFY(read(folder.filePath("output/CONVERSION.txt")).contains("nesting"));
}
void InterchangeTests::markdown_rejection_keeps_existing_data_and_files()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkpath("input/attachments"));
    put(folder.filePath("input/attachments/file"), "bytes");
    put(folder.filePath("input/a.md"), "# A\n\n[file](attachments/file)\n\n[bad](attachments/../outside)\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto existing = w.create_page(db.id, "Keep");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
    QCOMPARE(w.pages(db.id).size(), std::size_t(1));
    QVERIFY(w.attachments().empty());
    QVERIFY(w.audit_attachments().empty());
    QCOMPARE(w.load_page(existing.document.id).revision, existing.revision);
    put(folder.filePath("input/a.md"), QByteArray(1, char(0xff)));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::export_markdown(w, path(folder.filePath("workspace"))));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             interchange::export_markdown(w, path(folder.filePath("workspace/attachments/export"))));
    w.trash_database(db.id, true);
    put(folder.filePath("input/a.md"), "# valid\n");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
}
void InterchangeTests::markdown_forward_missing_and_duplicate_anchors()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkdir("input"));
    put(folder.filePath("input/a.md"),
        "# A\n\n[forward](b.md#block-target) [missing](absent.md) [missing block](b.md#block-gone)\n");
    put(folder.filePath("input/b.md"), "# B\n\n<a id=\"block-target\"></a>\n## Target\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto report = interchange::import_markdown(w, path(folder.filePath("input")), db.id);
    const auto a = w.load_page(report.pages[0]), b = w.load_page(report.pages[1]);
    const auto links = w.outgoing_links(a.document.id);
    QCOMPARE(links.size(), std::size_t(3));
    QCOMPARE(links[0].target_id, b.document.blocks[0].id);
    QCOMPARE(links[1].unresolved_name, std::string("absent.md"));
    QCOMPARE(links[2].kind, ReferenceKind::Block);
    QCOMPARE(w.pages(db.id).size(), std::size_t(2));
    put(folder.filePath("input/b.md"), "# B\n\n<a id=\"block-target\"></a>\nOne\n\n<a id=\"block-target\"></a>\nTwo\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
    QCOMPARE(w.pages(db.id).size(), std::size_t(2));
}
void InterchangeTests::markdown_sql_failure_and_missing_export_file_leave_no_partial_data()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkpath("input/attachments"));
    put(folder.filePath("input/attachments/file"), "bytes");
    put(folder.filePath("input/a.md"), "# New\n\n[file](attachments/file)\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Keep");
    sqlite3* raw = nullptr;
    QCOMPARE(sqlite3_open(folder.filePath("workspace/nebula.sqlite3").toUtf8().constData(), &raw), SQLITE_OK);
    std::unique_ptr<sqlite3, DatabaseCloser> connection(raw);
    // Nebula connections run with triggers disabled; a missing derived table fails the import.
    QCOMPARE(sqlite3_exec(raw, "DROP TABLE page_search", nullptr, nullptr, nullptr), SQLITE_OK);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
    QCOMPARE(w.pages(db.id).size(), std::size_t(1));
    QVERIFY(w.attachments().empty());
    QVERIFY(w.audit_attachments().empty());
    w.rebuild_search();
    const auto imported =
        w.import_attachment(page, page.document.blocks[0].id, path(folder.filePath("input/attachments/file")), false);
    QVERIFY(QFile::remove(QString::fromStdString(w.attachment_path(imported.file.id).u8string())));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::export_markdown(w, path(folder.filePath("failed-export"))));
    QVERIFY(!QFile::exists(folder.filePath("failed-export")));
    QCOMPARE(w.load_page(page.document.id).revision, page.revision);
}
void InterchangeTests::markdown_image_inline_asset_and_unsafe_identity_roundtrip()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Original", {}, {{u8"見出し /#?", {}, BlockKind::Heading, {{"Target"}}}});
    put(folder.filePath("image.png"),
        QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="));
    const auto image = w.import_attachment(page, page.document.blocks[0].id, path(folder.filePath("image.png")), true);
    page.document.blocks.back().runs = {
        {"inline "},
        {"", true, false, {}, Asset{true, "preview", image.file.id}},
        {" target", false, false, Reference{ReferenceKind::Block, page.document.blocks[0].id}}};
    w.save_page(page);
    const auto exported = interchange::export_markdown(w, path(folder.filePath("markdown")));
    QVERIFY(!exported.warnings.empty());
    const auto report = interchange::import_markdown(w, path(folder.filePath("markdown")), db.id);
    const auto copy = w.load_page(report.pages[0]);
    QCOMPARE(copy.document.blocks[1].kind, BlockKind::Image);
    QVERIFY(copy.document.blocks.back().runs[1].asset);
    QCOMPARE(copy.document.blocks.back().runs[1].asset->attachment_id, copy.document.blocks[1].attachment_id);
    QCOMPARE(copy.document.blocks.back().runs.back().reference->target_id, copy.document.blocks[0].id);
    QCOMPARE(read(QString::fromStdString(w.attachment_path(copy.document.blocks[1].attachment_id).u8string())),
             read(folder.filePath("image.png")));
}
void InterchangeTests::json_roundtrip_preserves_the_full_model_and_retained_files()
{
    QTemporaryDir folder;
    const auto root = folder.filePath("workspace");
    auto w = Workspace::open(path(root));
    const auto db = w.create_database("Notes"), journal = w.create_database("Journal");
    const auto title = w.add_property(db.id, "Text", PropertyKind::Text),
               score = w.add_property(db.id, "Score", PropertyKind::Number),
               date = w.add_property(db.id, "Entry date", PropertyKind::Date),
               done = w.add_property(db.id, "Done", PropertyKind::Checkbox),
               tags = w.add_property(db.id, "Tags", PropertyKind::Tags),
               unset = w.add_property(db.id, "Unset", PropertyKind::Text);
    auto target = w.create_page(db.id, "Duplicate", {},
                                {{"heading", {}, BlockKind::Heading, {{u8"見出し", true, true}}},
                                 {"child", "heading", BlockKind::Paragraph, {{"deleted child"}}}});
    auto source =
        w.create_page(db.id, "Duplicate",
                      {{title.id, std::string("text\0after", 10)},
                       {score.id, 42.5},
                       {date.id, Date{"2024-02-29"}},
                       {done.id, false},
                       {tags.id, std::vector<std::string>{"comma,tag", u8"日本語"}},
                       {unset.id, {}}},
                      {{"source",
                        {},
                        BlockKind::Paragraph,
                        {{"default", false, false, Reference{ReferenceKind::Page, target.document.id, {}, {}, false}},
                         {" rich ", true, true},
                         {"custom", true, false, Reference{ReferenceKind::Block, "heading"}},
                         {"future", false, false, Reference{ReferenceKind::Page, {}, "Future", journal.id, false}},
                         {"unavailable", false, true, Reference{ReferenceKind::Block, "missing-stable-target"}}}},
                       {"bullet", {}, BlockKind::Bullet, {{u8"Unicode 😀\u2028soft"}}},
                       {"todo", {}, BlockKind::Todo, {{"completed"}}, {}, true},
                       {"divider", {}, BlockKind::Divider}});
    put(folder.filePath("file.txt"), "retained bytes");
    const auto file = w.import_attachment(source, "divider", path(folder.filePath("file.txt")), false);
    for (auto& block : source.document.blocks)
        if (block.id == file.card)
            block.attachment_id.clear();
    w.save_page(source); // Keep registered bytes even when no current object references them.
    put(folder.filePath("image.png"), "image bytes");
    const auto image = w.import_attachment(source, file.following, path(folder.filePath("image.png")), true);
    source.document.blocks[0].runs.push_back({"", false, true, {}, Asset{true, "inline image", image.file.id}});
    w.save_page(source);
    target.document.blocks.pop_back();
    w.save_page(target);
    target.document.blocks.push_back({"restored", {}, BlockKind::Paragraph, {{"restored fragment"}}});
    w.save_page(target);
    target.document.blocks.pop_back();
    w.save_page(target);
    for (const auto& group : w.deleted_blocks())
        if (group.label == "restored fragment")
            target = w.restore_deleted_blocks(group.id);
    View view;
    view.database_id = db.id;
    view.name = "Filtered";
    view.visible_properties = {date.id, title.id, tags.id, done.id};
    view.filters = {{score.id, Comparison::Greater, 1.0}, {tags.id, Comparison::Contains, std::string("comma,tag")}};
    view.sort_property = score.id;
    view.ascending = false;
    w.save_view(view);
    const auto trashed =
        w.create_page(journal.id, "Trashed", {},
                      {{"trashed",
                        {},
                        BlockKind::Paragraph,
                        {{"keep trash", false, false, Reference{ReferenceKind::Page, source.document.id}}}}});
    w.trash_page(trashed.document.id, true);
    w.trash_database(journal.id, true);
    QDir(root).mkpath("attachments/legacy");
    put(root + "/attachments/legacy/old.txt", "legacy bytes");
    sql(root + "/nebula.sqlite3", "UPDATE pages SET revision=9007199254740993 WHERE title='Duplicate'; DELETE FROM "
                                  "page_search; DELETE FROM links");
    interchange::export_json(w, path(folder.filePath("export")));
    const auto before = object(folder.filePath("export/workspace.json"));
    auto imported = interchange::import_json(path(folder.filePath("export")), path(folder.filePath("imported")));
    QCOMPARE(imported.id(), w.id());
    QCOMPARE(imported.load_page(source.document.id).revision, std::int64_t(9007199254740993LL));
    QCOMPARE(encode_document(imported.load_page(source.document.id).document),
             encode_document(w.load_page(source.document.id).document));
    QCOMPARE(std::get<Date>(imported.load_page(source.document.id).properties.at(date.id)).iso,
             std::string("2024-02-29"));
    QCOMPARE(imported.views(db.id)[0].visible_properties, view.visible_properties);
    QCOMPARE(imported.backlinks(ReferenceKind::Page, target.document.id).size(), std::size_t(2));
    QCOMPARE(imported.search("Unicode").size(), std::size_t(1));
    QCOMPARE(imported.databases(true).size(), std::size_t(2));
    QCOMPARE(imported.databases().size(), std::size_t(1));
    QCOMPARE(imported.deleted_blocks().size(), std::size_t(1));
    QCOMPARE(imported.deleted_blocks()[0].label, std::string("deleted child"));
    QCOMPARE(read(QString::fromStdString(imported.attachment_path(file.file.id).u8string())),
             QByteArray("retained bytes"));
    interchange::export_json(imported, path(folder.filePath("again")));
    QCOMPARE(object(folder.filePath("again/workspace.json")), before);
    const auto original = read(root + "/nebula.sqlite3");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")), path(root)));
    QCOMPARE(read(root + "/nebula.sqlite3"), original);
    imported.backup_to(path(folder.filePath("backup")));
    auto restored = Workspace::restore(path(folder.filePath("backup")), path(folder.filePath("restored")));
    QCOMPARE(restored.load_page(source.document.id).revision, std::int64_t(9007199254740993LL));
}
void InterchangeTests::json_rejects_unsupported_malformed_and_colliding_rows()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto property = w.add_property(db.id, "Text", PropertyKind::Text);
    auto page = w.create_page(db.id, "Keep", {{property.id, std::string("valid")}});
    View view;
    view.database_id = db.id;
    view.name = "All";
    view.visible_properties = {property.id};
    w.save_view(view);
    page.document.blocks.push_back({"removed", {}, BlockKind::Paragraph, {{"archive"}}});
    w.save_page(page);
    page.document.blocks.pop_back();
    w.save_page(page);
    interchange::export_json(w, path(folder.filePath("export")));
    const auto file = folder.filePath("export/workspace.json");
    const auto valid = object(file);
    const auto rows = valid["tables"].toObject();
    std::vector<QJsonObject> bad;
    auto modified = valid;
    modified["version"] = 99;
    bad.push_back(modified);
    modified = valid;
    modified["new_feature"] = true;
    bad.push_back(modified);
    modified = valid;
    modified.remove("tables");
    bad.push_back(modified);
    auto add = [&](const char* table, const std::function<void(QJsonObject&)>& mutate) {
        auto data = rows;
        auto list = data[table].toArray();
        auto row = list[0].toObject();
        mutate(row);
        list[0] = row;
        data[table] = list;
        auto value = valid;
        value["tables"] = data;
        bad.push_back(value);
    };
    add("pages", [](auto& row) { row["revision"] = 9007199254740992.0; });
    add("pages", [](auto& row) { row["revision"] = "9223372036854775808"; });
    add("pages", [](auto& row) { row["trashed"] = "false"; });
    add("pages", [](auto& row) { row["database_id"] = "missing"; });
    add("pages", [](auto& row) { row["created_at"] = "not a timestamp"; });
    add("blocks", [](auto& row) { row["parent_id"] = "missing"; });
    add("blocks", [](auto& row) { row["position"] = "8"; });
    add("blocks", [](auto& row) {
        auto payload = row["payload"].toObject();
        payload["version"] = 99;
        row["payload"] = payload;
    });
    add("blocks", [](auto& row) {
        auto payload = row["payload"].toObject();
        payload["new_formatting"] = true;
        row["payload"] = payload;
    });
    add("databases", [](auto& row) { row["id"] = ""; });
    add("property_values",
        [](auto& row) { row["value_json"] = QJsonObject{{"version", 1}, {"type", 2}, {"value", 5.0}}; });
    add("saved_views", [](auto& row) {
        auto settings = row["settings"].toObject();
        settings["visible"] = QJsonArray{"unknown-column"};
        row["settings"] = settings;
    });
    add("deleted_blocks", [](auto& row) {
        auto fragment = row["fragment"].toObject();
        fragment["id"] = "wrong-page";
        row["fragment"] = fragment;
    });
    {
        auto data = rows;
        auto list = data["pages"].toArray();
        list.append(list[0]);
        data["pages"] = list;
        modified = valid;
        modified["tables"] = data;
        bad.push_back(modified);
    }
    for (std::size_t i = 0; i < bad.size(); ++i) {
        put(file, bad[i]);
        const auto output = folder.filePath("reject-" + QString::number(i));
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 interchange::import_json(path(folder.filePath("export")), path(output)));
        QVERIFY(!QFile::exists(output));
        QCOMPARE(w.pages(db.id).size(), std::size_t(1));
    }
    put(file, valid);
    put(file, QByteArray(1, char(0xff)));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("invalid-utf8"))));
}
void InterchangeTests::json_rejects_missing_changed_unsafe_and_unlisted_files()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Keep");
    put(folder.filePath("file"), "bytes");
    const auto imported = w.import_attachment(page, page.document.blocks[0].id, path(folder.filePath("file")), false);
    interchange::export_json(w, path(folder.filePath("export")));
    const auto file = folder.filePath("export/workspace.json");
    const auto valid = object(file);
    const auto attachment = folder.filePath("export/attachments/" + QString::fromStdString(imported.file.id));
    put(attachment, "changed");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("changed"))));
    QVERIFY(!QFile::exists(folder.filePath("changed")));
    put(attachment, "bytes");
    auto files = valid["files"].toArray();
    auto entry = files[0].toObject();
    entry["path"] = "../outside";
    files[0] = entry;
    auto modified = valid;
    modified["files"] = files;
    put(file, modified);
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error, interchange::import_json(path(folder.filePath("export")), path(folder.filePath("unsafe"))));
    put(file, valid);
    put(folder.filePath("export/attachments/unlisted"), "extra");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_json(path(folder.filePath("export")), path(folder.filePath("extra"))));
    QVERIFY(QFile::remove(folder.filePath("export/attachments/unlisted")));
    QVERIFY(QFile::remove(attachment));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("missing"))));
    QVERIFY(!QFile::exists(folder.filePath("missing")));
}
void InterchangeTests::json_rejects_duplicate_keys_including_escaped_names()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    w.create_page(db.id, "Keep");
    interchange::export_json(w, path(folder.filePath("export")));
    const auto file = folder.filePath("export/workspace.json");
    const auto original = read(file);
    auto duplicate = original;
    duplicate.insert(1, "\"version\":1,");
    put(file, duplicate);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("duplicate"))));
    duplicate = original;
    duplicate.replace("\"checked\": false", "\"checked\":false,\"check\\u0065d\":false");
    QVERIFY(duplicate != original);
    put(file, duplicate);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("escaped"))));
}
void InterchangeTests::json_preserves_reference_filters_and_reads_schema_eight_exports()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto target = w.create_page(db.id, "Target");
    const auto source =
        w.create_page(db.id, "Source", {},
                      {{"source",
                        {},
                        BlockKind::Paragraph,
                        {{"link", false, false, Reference{ReferenceKind::Block, target.document.blocks[0].id}}}}});
    View view;
    view.database_id = db.id;
    view.name = "References";
    view.link_filters = {{LinkRelation::ReferencesPage, target.document.id}};
    w.save_view(view);
    interchange::export_json(w, path(folder.filePath("export")));
    auto imported = interchange::import_json(path(folder.filePath("export")), path(folder.filePath("imported")));
    QCOMPARE(imported.views(db.id)[0].link_filters[0].page_id, target.document.id);
    QCOMPARE(imported.query_view(imported.views(db.id)[0])[0].document.id, source.document.id);
    const auto file = folder.filePath("export/workspace.json");
    auto legacy = object(file);
    legacy["schema"] = 8;
    auto data = legacy["tables"].toObject();
    auto views = data["saved_views"].toArray();
    auto row = views[0].toObject();
    auto settings = row["settings"].toObject();
    settings["version"] = 1;
    settings.remove("link_filters");
    row["settings"] = settings;
    views[0] = row;
    data["saved_views"] = views;
    legacy["tables"] = data;
    put(file, legacy);
    auto old = interchange::import_json(path(folder.filePath("export")), path(folder.filePath("legacy")));
    QVERIFY(old.views(db.id)[0].link_filters.empty());
    QCOMPARE(old.query_view(old.views(db.id)[0]).size(), std::size_t(2));
    settings["version"] = 2;
    settings["link_filters"] = QJsonArray{};
    row["settings"] = settings;
    views[0] = row;
    data["saved_views"] = views;
    legacy["tables"] = data;
    put(file, legacy);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("mismatched-schema"))));
}
QTEST_GUILESS_MAIN(InterchangeTests)
#include "interchange_tests.moc"
