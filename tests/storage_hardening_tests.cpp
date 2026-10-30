// Regression checks for the 2026-10-30 review suggestions (SUG-xx in ROADMAP.md).
#include "nebula/attachments/files.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include "nebula/storage/internals.hpp"
#include "nebula/storage/workspace.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <sqlite3.h>
#include <sys/stat.h>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& p)
{
    return std::filesystem::u8path(p.toUtf8().constData());
}
void execute_sql(const QString& file, const char* command)
{
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open(file.toUtf8().constData(), &raw);
    std::unique_ptr<sqlite3, DatabaseCloser> db(raw);
    if (opened != SQLITE_OK || sqlite3_exec(raw, command, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(raw));
}
std::string text(const QString& file, const char* command, int column = 0)
{
    sqlite3* raw = nullptr;
    sqlite3_open(file.toUtf8().constData(), &raw);
    std::unique_ptr<sqlite3, DatabaseCloser> db(raw);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(raw, command, -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(raw));
    std::string result;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        if (!result.empty())
            result += "\n";
        const auto* value = sqlite3_column_text(statement, column);
        result += value ? reinterpret_cast<const char*>(value) : "";
    }
    sqlite3_finalize(statement);
    return result;
}
QByteArray bytes(const QString& file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return in.readAll();
}
void put(const QString& file, const QByteArray& data)
{
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
        throw std::runtime_error("Cannot write test file");
}
unsigned mode(const QString& file)
{
    struct stat status{};
    ::lstat(file.toUtf8().constData(), &status);
    return status.st_mode & 0777;
}
} // namespace

class StorageHardeningTests : public QObject {
    Q_OBJECT
private slots:
    void untrusted_schema_objects_are_refused();
    void new_workspace_files_are_private();
    void backup_and_export_folders_are_not_opened_as_workspaces();
    void another_process_cannot_open_an_open_workspace();
    void merged_blocks_are_not_archived_and_membership_is_indexed();
    void fragment_returns_as_root_when_its_parent_moved_away();
    void journal_bootstrap_and_dated_entries_are_atomic();
    void views_sort_and_filter_with_case_insensitive_rules();
    void saves_rewrite_only_changed_rows_and_reindex_only_label_changes();
    void reverse_link_queries_use_the_source_index();
    void search_rows_store_titles_once_and_find_cross_block_terms();
    void staged_imports_report_progress_and_cancel_cleanly();
    void integrity_checks_are_recorded_and_report_problems();
    void version_nine_migrates_derived_structures();
    void identities_follow_the_documented_grammar();
    void empty_trash_purges_content_files_and_free_pages();
    void views_and_properties_can_be_renamed_and_deleted();
    void summaries_and_link_targets_avoid_document_loading();
    void symlinked_components_are_refused_beneath_a_root();
    void large_backups_copy_in_steps();
    void export_snapshot_is_private_read_only_and_independent_of_later_saves();
    void imported_revisions_stay_below_the_overflow_bound();
    void markdown_attachments_accept_common_syntax_and_report_missing_files();
};

void StorageHardeningTests::untrusted_schema_objects_are_refused()
{
    for (const char* injected : {"CREATE TRIGGER wipe AFTER UPDATE ON pages BEGIN DELETE FROM blocks; END;",
                                 "CREATE VIEW leak AS SELECT * FROM pages;", "CREATE TABLE extra(value TEXT);"}) {
        QTemporaryDir folder;
        {
            auto w = Workspace::open(path(folder.path()));
            w.create_page(w.create_database("Notes").id, "Keep");
        }
        execute_sql(folder.filePath("nebula.sqlite3"), injected);
        const auto before = bytes(folder.filePath("nebula.sqlite3"));
        QVERIFY_THROWS_EXCEPTION(StorageError, Workspace::open(path(folder.path())));
        QVERIFY_THROWS_EXCEPTION(StorageError, Workspace::read_only(path(folder.path())));
        QCOMPARE(bytes(folder.filePath("nebula.sqlite3")), before);
    }
}
void StorageHardeningTests::new_workspace_files_are_private()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("diary")));
    QCOMPARE(mode(folder.filePath("diary")), 0700u);
    QCOMPARE(mode(folder.filePath("diary/nebula.sqlite3")), 0600u);
    QCOMPARE(mode(folder.filePath("diary/attachments")), 0700u);
    w.create_page(w.create_database("Notes").id, "Private");
    w.backup_to(path(folder.filePath("backup")));
    QCOMPARE(mode(folder.filePath("backup")), 0700u);
    QCOMPARE(mode(folder.filePath("backup/nebula.sqlite3")), 0600u);
    QCOMPARE(mode(folder.filePath("backup/attachments")), 0700u);
    interchange::export_json(w, path(folder.filePath("export")));
    QCOMPARE(mode(folder.filePath("export")), 0700u);
    QCOMPARE(mode(folder.filePath("export/workspace.json")), 0600u);
    auto restored = Workspace::restore(path(folder.filePath("backup")), path(folder.filePath("restored")));
    QCOMPARE(mode(folder.filePath("restored")), 0700u);
    QCOMPARE(mode(folder.filePath("restored/nebula.sqlite3")), 0600u);
}
void StorageHardeningTests::backup_and_export_folders_are_not_opened_as_workspaces()
{
    QTemporaryDir folder;
    {
        auto w = Workspace::open(path(folder.filePath("source")));
        w.create_page(w.create_database("Notes").id, "Keep");
        w.backup_to(path(folder.filePath("backup")));
        interchange::export_json(w, path(folder.filePath("json")));
        interchange::export_markdown(w, path(folder.filePath("markdown")));
    }
    for (const auto* name : {"backup", "json", "markdown"}) {
        const auto entries =
            QDir(folder.filePath(name)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        try {
            Workspace::open(path(folder.filePath(name)));
            QFAIL("Opening an export or backup should be refused");
        } catch (const StorageError& error) {
            QCOMPARE(error.kind(), ErrorKind::Unsupported);
        }
        QCOMPARE(QDir(folder.filePath(name)).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden),
                 entries);
    }
}
void StorageHardeningTests::another_process_cannot_open_an_open_workspace()
{
    QTemporaryDir folder;
    QVERIFY(QDir(folder.path()).mkpath("markdown"));
    put(folder.filePath("markdown/a.md"), "# Imported\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    auto again = Workspace::open(path(folder.filePath("workspace"))); // Same process: shared session.
    QCOMPARE(again.id(), w.id());
    QProcess process;
    process.start(
        QCoreApplication::applicationDirPath() + "/nebula_workspace",
        {"import-md", folder.filePath("markdown"), folder.filePath("workspace"), QString::fromStdString(db.id)});
    QVERIFY(process.waitForFinished(10000));
    QCOMPARE(process.exitCode(), 1);
    QVERIFY(process.readAllStandardError().contains("already open"));
    QVERIFY(w.pages(db.id).empty());
}
void StorageHardeningTests::merged_blocks_are_not_archived_and_membership_is_indexed()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Merge", {},
                              {{"a", "", BlockKind::Paragraph, {{"alpha"}}},
                               {"b", "", BlockKind::Paragraph, {{"bravo"}}},
                               {"c", "", BlockKind::Paragraph, {{"charlie"}}}});
    page.document.blocks[0].runs = {{"alphabravo"}};
    page.document.blocks.erase(page.document.blocks.begin() + 1);
    w.save_page(page, {"b"});
    QVERIFY(w.deleted_blocks().empty());
    page.document.blocks.pop_back();
    w.save_page(page);
    QCOMPARE(w.deleted_blocks().size(), std::size_t(1));
    QCOMPARE(text(folder.filePath("nebula.sqlite3"), "SELECT block_id FROM deleted_block_members"), std::string("c"));
    page.document.blocks.push_back({"c", "", BlockKind::Paragraph, {{"charlie"}}});
    w.save_page(page);
    QVERIFY(w.deleted_blocks().empty());
}
void StorageHardeningTests::fragment_returns_as_root_when_its_parent_moved_away()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    auto source = w.create_page(db.id, "Source", {},
                                {{"x", "", BlockKind::Paragraph, {{"Parent"}}},
                                 {"y", "x", BlockKind::Paragraph, {{"Child"}}},
                                 {"z", "", BlockKind::Paragraph, {{"Keep"}}}});
    auto destination = w.create_page(db.id, "Destination");
    source.document.blocks.erase(source.document.blocks.begin() + 1);
    w.save_page(source);
    const auto group = w.deleted_blocks()[0].id;
    w.transfer_blocks(source, destination, "x");
    bool reparented = false;
    const auto restored = w.restore_deleted_blocks(group, &reparented);
    QVERIFY(reparented);
    QCOMPARE(restored.document.blocks.back().id, std::string("y"));
    QVERIFY(restored.document.blocks.back().parent_id.empty());
    QVERIFY(w.deleted_blocks().empty());
}
void StorageHardeningTests::journal_bootstrap_and_dated_entries_are_atomic()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto journal = w.create_journal();
    QCOMPARE(w.properties(journal.id).size(), std::size_t(3));
    QCOMPARE(w.views(journal.id).size(), std::size_t(1));
    const auto notes = w.create_database("Notes");
    execute_sql(folder.filePath("nebula.sqlite3"), "DROP TABLE page_search");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, w.create_dated_entry(notes.id, "2026-10-30"));
    QVERIFY(w.properties(notes.id).empty());
    QVERIFY(w.pages(notes.id).empty());
    w.rebuild_search();
    const auto entry = w.create_dated_entry(notes.id, "2024-02-29");
    QCOMPARE(entry.title, std::string("2024-02-29"));
    QCOMPARE(w.properties(notes.id)[0].name, std::string("Entry date"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, w.create_dated_entry(notes.id, "2023-02-29"));
}
void StorageHardeningTests::views_sort_and_filter_with_case_insensitive_rules()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    const auto tags = w.add_property(db.id, "Tags", PropertyKind::Tags);
    for (const auto* title : {"Zebra", "apple", "Item 10", "Item 9"})
        w.create_page(db.id, title, {{tags.id, std::vector<std::string>{"work", "home"}}});
    View view;
    view.database_id = db.id;
    view.name = "Sorted";
    w.save_view(view);
    std::vector<std::string> titles;
    for (const auto& row : w.query_view(view))
        titles.push_back(row.title);
    QCOMPARE(titles, (std::vector<std::string>{"apple", "Item 9", "Item 10", "Zebra"}));
    QVERIFY(w.query_view(view)[0].document.blocks.empty());
    view.filters = {{"", Comparison::Contains, std::string("ZEB")}};
    QCOMPARE(w.query_view(view).size(), std::size_t(1));
    view.filters = {{tags.id, Comparison::Equal, std::vector<std::string>{"home", "work"}}};
    QCOMPARE(w.query_view(view).size(), std::size_t(4));
}
void StorageHardeningTests::saves_rewrite_only_changed_rows_and_reindex_only_label_changes()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    auto target =
        w.create_page(db.id, "Target", {},
                      {{"t1", "", BlockKind::Heading, {{"Heading"}}}, {"t2", "", BlockKind::Paragraph, {{"Body"}}}});
    auto source =
        w.create_page(db.id, "Source", {},
                      {{"s1",
                        "",
                        BlockKind::Paragraph,
                        {{"label", false, false, Reference{ReferenceKind::Page, target.document.id, {}, {}, false}}}}});
    const auto file = folder.filePath("nebula.sqlite3");
    const auto block_rows = text(file, "SELECT id||':'||rowid FROM blocks WHERE page_id IN (SELECT id FROM pages WHERE "
                                       "title='Target') ORDER BY position");
    const auto referrer_rows = text(
        file,
        "SELECT row FROM page_search_rows WHERE page_id IN (SELECT id FROM pages WHERE title='Source') ORDER BY row");
    target.document.blocks[1].runs = {{"Body edited"}};
    w.save_page(target);
    QCOMPARE(text(file, "SELECT id||':'||rowid FROM blocks WHERE page_id IN (SELECT id FROM pages WHERE "
                        "title='Target') ORDER BY position"),
             block_rows);
    QCOMPARE(text(file, "SELECT row FROM page_search_rows WHERE page_id IN (SELECT id FROM pages WHERE title='Source') "
                        "ORDER BY row"),
             referrer_rows);
    QCOMPARE(w.search("edited").size(), std::size_t(1));
    target.title = "Renamed";
    w.save_page(target);
    QVERIFY(text(file, "SELECT row FROM page_search_rows WHERE page_id IN (SELECT id FROM pages WHERE title='Source') "
                       "ORDER BY row") != referrer_rows);
    QCOMPARE(w.search("Renamed").size(), std::size_t(2));
    std::swap(target.document.blocks[0], target.document.blocks[1]);
    w.save_page(target);
    QCOMPARE(w.load_page(target.document.id).document.blocks[0].id, std::string("t2"));
}
void StorageHardeningTests::reverse_link_queries_use_the_source_index()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    Q_UNUSED(w);
    const auto plan =
        text(folder.filePath("nebula.sqlite3"), "EXPLAIN QUERY PLAN SELECT 1 FROM links WHERE source_page='x'", 3);
    QVERIFY2(QString::fromStdString(plan).contains("links_source"), plan.c_str());
}
void StorageHardeningTests::search_rows_store_titles_once_and_find_cross_block_terms()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Mountain", {},
                              {{"one", "", BlockKind::Paragraph, {{"river crossing"}}},
                               {"two", "", BlockKind::Paragraph, {{"forest camp"}}},
                               {"three", "", BlockKind::Paragraph, {}}});
    const auto file = folder.filePath("nebula.sqlite3");
    QCOMPARE(text(file, "SELECT count(*) FROM page_search"), std::string("3"));
    QCOMPARE(text(file, "SELECT count(*) FROM page_search WHERE title<>''"), std::string("1"));
    auto title = w.search("mountain");
    QCOMPARE(title.size(), std::size_t(1));
    QVERIFY(title[0].block_id.empty());
    auto spread = w.search("river camp");
    QCOMPARE(spread.size(), std::size_t(1));
    QVERIFY(spread[0].block_id.empty());
    auto local = w.search("forest camp");
    QCOMPARE(local[0].block_id, std::string("two"));
    QCOMPARE(local[0].context, std::string("forest camp"));
    auto mixed = w.search("mountain river");
    QCOMPARE(mixed.size(), std::size_t(1));
}
void StorageHardeningTests::staged_imports_report_progress_and_cancel_cleanly()
{
    QTemporaryDir folder;
    const auto original = folder.filePath("large.bin");
    put(original, QByteArray(300000, 'x'));
    auto w = Workspace::open(path(folder.filePath("workspace")));
    auto page = w.create_page(w.create_database("Notes").id, "Files");
    {
        auto staged = w.prepare_attachment(path(original), false);
        QVERIFY_THROWS_EXCEPTION(StorageError,
                                 staged.copy([](std::int64_t copied, std::int64_t) { return copied < 65536; }));
    }
    QVERIFY(QDir(folder.filePath("workspace/attachments")).entryList(QDir::Files | QDir::Hidden).isEmpty());
    std::atomic<int> calls{0};
    auto staged = w.prepare_attachment(path(original), false);
    staged.copy([&](std::int64_t copied, std::int64_t total) {
        ++calls;
        return copied <= total;
    });
    QVERIFY(calls >= 4);
    const auto imported = w.commit_attachment(page, page.document.blocks[0].id, staged);
    QCOMPARE(imported.file.size, std::int64_t(300000));
    QVERIFY(w.audit_attachments().empty());
}
void StorageHardeningTests::integrity_checks_are_recorded_and_report_problems()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    w.create_database("Notes");
    const auto file = folder.filePath("nebula.sqlite3");
    QVERIFY(!text(file, "SELECT value FROM maintenance WHERE name='integrity_checked_at'").empty());
    QVERIFY(w.check_integrity().ok());
    execute_sql(file, "PRAGMA foreign_keys=OFF; INSERT INTO pages(id,database_id,title,revision) "
                      "VALUES('orphan','missing-db','Orphan',1)");
    const auto report = w.check_integrity();
    QVERIFY(!report.ok());
    QVERIFY(QString::fromStdString(report.problems[0]).contains("pages"));
}
void StorageHardeningTests::version_nine_migrates_derived_structures()
{
    QTemporaryDir folder;
    std::string id, fragment;
    {
        auto w = Workspace::open(path(folder.path()));
        auto page = w.create_page(
            w.create_database("Notes").id, "Legacy", {},
            {{"a", "", BlockKind::Paragraph, {{"keep"}}}, {"b", "", BlockKind::Paragraph, {{"removed"}}}});
        id = page.document.id;
        page.document.blocks.pop_back();
        w.save_page(page);
        fragment = w.deleted_blocks()[0].id;
    }
    execute_sql(folder.filePath("nebula.sqlite3"),
                "DROP TABLE deleted_block_members; DROP TABLE maintenance; DROP TABLE "
                "page_search_rows; DROP TABLE page_search; DROP INDEX links_source;"
                "CREATE VIRTUAL TABLE page_search USING fts5(page_id UNINDEXED,block_id "
                "UNINDEXED,title,properties,body,excerpt UNINDEXED); PRAGMA user_version=9");
    auto w = Workspace::open(path(folder.path()));
    QCOMPARE(text(folder.filePath("nebula.sqlite3"), "PRAGMA user_version"), std::string("10"));
    QCOMPARE(text(folder.filePath("nebula.sqlite3"), "SELECT fragment_id FROM deleted_block_members"), fragment);
    QCOMPARE(w.search("keep").size(), std::size_t(1));
    QCOMPARE(w.restore_deleted_blocks(fragment).document.blocks.size(), std::size_t(2));
}
void StorageHardeningTests::identities_follow_the_documented_grammar()
{
    QVERIFY(valid_identity("0123456789abcdef0123456789abcdef"));
    QVERIFY(valid_identity(u8"日本-id_1.x"));
    QVERIFY(!valid_identity(""));
    QVERIFY(!valid_identity(std::string(129, 'a')));
    QVERIFY(!valid_identity("bad\nid"));
    QVERIFY(!valid_identity(std::string(1, char(0xff))));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             validate(Page{"page", {{"tab\tid", "", BlockKind::Paragraph, {}}}}));
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        decode_document(
            R"({"version":1,"id":"p","blocks":[{"id":"a\u0001","parent":"","kind":0,"payload":{"version":3,"runs":[],"asset_label":"","checked":false,"attachment_id":""}}]})"));
}
void StorageHardeningTests::empty_trash_purges_content_files_and_free_pages()
{
    QTemporaryDir folder;
    const auto original = folder.filePath("photo.bin");
    put(original, "unique-bytes-to-purge");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes"), old = w.create_database("Old");
    auto keep = w.create_page(db.id, "Keep");
    auto gone = w.create_page(db.id, "Secret diary", {}, {{"g", "", BlockKind::Paragraph, {{"confidential words"}}}});
    w.create_page(old.id, "Old page");
    w.add_property(old.id, "Old property", PropertyKind::Text);
    keep.document.blocks[0].runs = {
        {"fallback", false, false, Reference{ReferenceKind::Page, gone.document.id, {}, {}, false}}};
    w.save_page(keep);
    const auto file = w.import_attachment(gone, gone.document.blocks[0].id, path(original), false).file;
    auto with_removed = w.load_page(keep.document.id);
    with_removed.document.blocks.push_back({"tmp", "", BlockKind::Paragraph, {{"removed block"}}});
    w.save_page(with_removed);
    with_removed.document.blocks.pop_back();
    w.save_page(with_removed);
    QCOMPARE(w.search("Secret").size(), std::size_t(2));
    w.trash_page(gone.document.id, true);
    w.trash_database(old.id, true);
    const auto report = w.empty_trash();
    QCOMPARE(report.pages, std::size_t(2));
    QCOMPARE(report.databases, std::size_t(1));
    QCOMPARE(report.fragments, std::size_t(1));
    QCOMPARE(report.files, std::size_t(1));
    QVERIFY(report.compacted);
    QVERIFY(!QFile::exists(folder.filePath("workspace/attachments/" + QString::fromStdString(file.id))));
    QVERIFY(w.deleted_blocks().empty());
    QCOMPARE(w.databases(true).size(), std::size_t(1));
    QCOMPARE(w.resolve_reference({ReferenceKind::Page, gone.document.id}).state, ReferenceState::Unavailable);
    QVERIFY(w.search("Secret").empty());
    QCOMPARE(w.search("fallback").size(), std::size_t(1));
    QVERIFY(!bytes(folder.filePath("workspace/nebula.sqlite3")).contains("confidential words"));
    QVERIFY(w.check_integrity().ok());
    QVERIFY(w.audit_attachments().empty());
}
void StorageHardeningTests::views_and_properties_can_be_renamed_and_deleted()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    const auto mood = w.add_property(db.id, "Mood", PropertyKind::Text),
               score = w.add_property(db.id, "Score", PropertyKind::Number);
    auto page = w.create_page(db.id, "Day", {{mood.id, std::string("calm")}, {score.id, 3.0}});
    View view;
    view.database_id = db.id;
    view.name = "Scored";
    view.visible_properties = {mood.id, score.id};
    view.sort_property = score.id;
    view.filters = {{score.id, Comparison::Greater, 1.0}};
    w.save_view(view);
    w.rename_property(mood.id, "Feeling");
    QCOMPARE(w.search("feeling calm").size(), std::size_t(1));
    w.delete_property(score.id);
    QCOMPARE(w.properties(db.id).size(), std::size_t(1));
    QVERIFY(!w.load_page(page.document.id).properties.count(score.id));
    QCOMPARE(w.load_page(page.document.id).revision, page.revision + 1);
    const auto cleaned = w.views(db.id)[0];
    QCOMPARE(cleaned.visible_properties, std::vector<std::string>{mood.id});
    QVERIFY(cleaned.sort_property.empty());
    QVERIFY(cleaned.filters.empty());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, w.save_page(page));
    w.delete_view(cleaned.id);
    QVERIFY(w.views(db.id).empty());
    QVERIFY_THROWS_EXCEPTION(StorageError, w.delete_view(cleaned.id));
}
void StorageHardeningTests::summaries_and_link_targets_avoid_document_loading()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto a = w.create_database("A"), b = w.create_database("B");
    w.create_page(b.id, "Second", {},
                  {{"h", "", BlockKind::Heading, {{"Heading"}}}, {"p", "", BlockKind::Paragraph, {}}});
    const auto first = w.create_page(a.id, "First");
    w.trash_page(first.document.id, true);
    QCOMPARE(w.page_summaries().size(), std::size_t(1));
    QCOMPARE(w.page_summaries(true).size(), std::size_t(2));
    QVERIFY(w.page_summaries(true)[0].trashed);
    QCOMPARE(w.page_summaries(true)[0].database_name, std::string("A"));
    const auto targets = w.link_targets();
    QCOMPARE(targets.size(), std::size_t(3));
    QCOMPARE(targets[0].kind, ReferenceKind::Page);
    QVERIFY(targets[1].heading);
    QCOMPARE(targets[2].label, std::string("Empty block"));
    QCOMPARE(*w.page_title(first.document.id), std::string("First"));
    QVERIFY(!w.page_title("missing"));
}
void StorageHardeningTests::symlinked_components_are_refused_beneath_a_root()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkpath("root/attachments"));
    QVERIFY(root.mkpath("outside"));
    put(folder.filePath("outside/secret"), "outside");
    put(folder.filePath("root/attachments/file"), "inside");
    QCOMPARE(files::digest_beneath(path(folder.filePath("root")), "attachments/file").size, std::int64_t(6));
    QVERIFY(QFile::link(folder.filePath("outside"), folder.filePath("root/linked")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, files::digest_beneath(path(folder.filePath("root")), "linked/secret"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             files::digest_beneath(path(folder.filePath("root")), "attachments/../attachments/file"));
}
void StorageHardeningTests::large_backups_copy_in_steps()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    std::string paragraph(4000, 'w');
    for (int i = 0; i < 120; ++i)
        w.create_page(db.id, "Page " + std::to_string(i), {},
                      {{"b" + std::to_string(i), "", BlockKind::Paragraph, {{paragraph}}}});
    QVERIFY(std::stoll(text(folder.filePath("workspace/nebula.sqlite3"), "PRAGMA page_count")) > 256);
    w.backup_to(path(folder.filePath("backup")));
    auto restored = Workspace::restore(path(folder.filePath("backup")), path(folder.filePath("restored")));
    QCOMPARE(restored.pages(db.id).size(), std::size_t(120));
    QVERIFY(restored.check_integrity().ok());
}

void StorageHardeningTests::imported_revisions_stay_below_the_overflow_bound()
{
    QTemporaryDir folder;
    {
        auto w = Workspace::open(path(folder.filePath("source")));
        w.create_page(w.create_database("Notes").id, "Revision");
        interchange::export_json(w, path(folder.filePath("export")));
    }
    const auto file = folder.filePath("export/workspace.json");
    auto document = QJsonDocument::fromJson(bytes(file)).object();
    auto tables = document["tables"].toObject();
    auto pages = tables["pages"].toArray();
    auto page = pages[0].toObject();
    page["revision"] = "4611686018427387904";
    pages[0] = page;
    tables["pages"] = pages;
    document["tables"] = tables;
    QVERIFY(QFile::remove(file));
    put(file, QJsonDocument(document).toJson());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, interchange::import_json(path(folder.filePath("export")),
                                                                          path(folder.filePath("imported"))));
    QVERIFY(!QFile::exists(folder.filePath("imported")));
    page["revision"] = "4611686018427387903";
    pages[0] = page;
    tables["pages"] = pages;
    document["tables"] = tables;
    QVERIFY(QFile::remove(file));
    put(file, QJsonDocument(document).toJson());
    auto imported = interchange::import_json(path(folder.filePath("export")), path(folder.filePath("imported")));
    auto record = imported.load_page(imported.page_summaries()[0].id);
    imported.save_page(record);
    QVERIFY_THROWS_EXCEPTION(StorageError, imported.save_page(record));
}

void StorageHardeningTests::markdown_attachments_accept_common_syntax_and_report_missing_files()
{
    QTemporaryDir folder;
    QDir root(folder.path());
    QVERIFY(root.mkpath("input/attachments"));
    put(folder.filePath("input/attachments/My photo.png"), "image bytes");
    put(folder.filePath("input/a.md"),
        "# Photos\n\n![](attachments/My%20photo.png)\n\n[gone](attachments/missing.txt) keep text\n");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    const auto db = w.create_database("Notes");
    const auto report = interchange::import_markdown(w, path(folder.filePath("input")), db.id);
    const auto page = w.load_page(report.pages[0]);
    QCOMPARE(page.document.blocks[0].kind, BlockKind::Image);
    QCOMPARE(page.document.blocks[0].asset_label, std::string("My photo.png"));
    QCOMPARE(w.attachments().size(), std::size_t(1));
    std::string second;
    for (const auto& run : page.document.blocks[1].runs)
        second += run.text;
    QCOMPARE(second, std::string("gone keep text"));
    QVERIFY(std::any_of(report.warnings.begin(), report.warnings.end(),
                        [](const auto& w) { return w.find("missing.txt") != std::string::npos; }));
    put(folder.filePath("input/a.md"), "[escape](attachments/..%2Foutside)\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::import_markdown(w, path(folder.filePath("input")), db.id));
}

void StorageHardeningTests::export_snapshot_is_private_read_only_and_independent_of_later_saves()
{
    QTemporaryDir folder;
    auto workspace = Workspace::open(path(folder.filePath("workspace")));
    auto page = workspace.create_page(workspace.create_database("Notes").id, "Before");
    const auto file = path(folder.filePath("private-snapshot.sqlite3"));
    files::create_private_file(file);
    auto snapshot = WorkspaceInternals::snapshot(workspace, file);
    page.title = "After";
    page.document.blocks.front().runs = {{"Later body"}};
    workspace.save_page(page);
    QCOMPARE(snapshot.load_page(page.document.id).title, std::string("Before"));
    QCOMPARE(workspace.load_page(page.document.id).title, std::string("After"));
    QVERIFY_THROWS_EXCEPTION(StorageError, snapshot.create_database("Forbidden"));
    struct stat status{};
    QCOMPARE(::stat(file.c_str(), &status), 0);
    QCOMPARE(status.st_mode & 0777, 0600u);
    interchange::export_json(workspace, path(folder.filePath("json")));
    interchange::export_markdown(workspace, path(folder.filePath("markdown")));
    QVERIFY(!QFile::exists(folder.filePath("json/.nebula-export-snapshot.sqlite3")));
    QVERIFY(!QFile::exists(folder.filePath("markdown/.nebula-export-snapshot.sqlite3")));
    QCOMPARE(workspace.load_page(page.document.id).title, std::string("After"));
    workspace.transaction([&] {
        QVERIFY_THROWS_EXCEPTION(StorageError,
                                 interchange::export_json(workspace, path(folder.filePath("uncommitted"))));
        QVERIFY(!QFile::exists(folder.filePath("uncommitted")));
    });
}

QTEST_GUILESS_MAIN(StorageHardeningTests)
#include "storage_hardening_tests.moc"
