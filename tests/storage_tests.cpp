#include "nebula/storage/workspace.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/app/page_session.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/attachments/manifest.hpp"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <sqlite3.h>
#include <limits>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <sys/stat.h>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& p) { return std::filesystem::u8path(p.toUtf8().constData()); }
void sql(const QString& file, const char* command) {
    sqlite3* raw=nullptr;
    const int opened=sqlite3_open(file.toUtf8().constData(),&raw);
    std::unique_ptr<sqlite3,DatabaseCloser> db(raw);
    if (opened!=SQLITE_OK || sqlite3_exec(raw,command,nullptr,nullptr,nullptr)!=SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(raw));
}
std::int64_t count(const QString& file, const char* command) {
    sqlite3* raw=nullptr; sqlite3_open(file.toUtf8().constData(),&raw); std::unique_ptr<sqlite3,DatabaseCloser> db(raw);
    sqlite3_stmt* statement=nullptr;
    if (sqlite3_prepare_v2(raw,command,-1,&statement,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(raw));
    const auto result=sqlite3_step(statement)==SQLITE_ROW?sqlite3_column_int64(statement,0):-1; sqlite3_finalize(statement); return result;
}
QByteArray bytes(const QString& file) { QFile in(file); if (!in.open(QIODevice::ReadOnly)) return {}; return in.readAll(); }
void put(const QString& file,const QByteArray& data) { QFile out(file); if (!out.open(QIODevice::WriteOnly) || out.write(data)!=data.size()) throw std::runtime_error("Cannot write test file"); }
}
class StorageTests : public QObject {
    Q_OBJECT
private slots:
    void dates_and_types();
    void typed_filter_and_sort();
    void payload_roundtrip_and_rejection();
    void upgrade_v1_preserves_identity();
    void failed_migration_rolls_back();
    void pages_and_properties_survive_reopen();
    void invalid_save_rolls_back_everything();
    void stale_revision_cannot_overwrite_newer_edits();
    void views_share_pages_and_survive_reopen();
    void trash_restore_and_link_rebuild();
    void backup_restore_preserves_documents_and_files();
    void backup_refuses_overwrite_and_unsafe_files();
    void session_retains_failed_edits_and_blocks_navigation();
    void command_line_backup_and_restore();
    void killed_process_before_and_after_commit();
    void unresolved_references_never_bind_by_title();
    void deleted_subtrees_survive_restart_and_restore();
    void partial_undo_then_restore_preserves_existing_edits();
    void restore_requires_the_deleted_parent_and_page();
    void version_two_upgrade_preserves_notes();
    void restoring_siblings_in_reverse_order_keeps_their_order();
    void version_three_upgrade_preserves_deleted_fragments();
    void partially_restored_sibling_still_anchors_recovery();
    void divider_migration_preserves_tree_links_and_recovery();
    void reference_lifecycle_follows_ids_and_current_owners();
    void transfer_undo_redo_survives_reopen_and_backup();
    void transfer_failure_rolls_back_both_pages_and_index();
    void transfer_rejects_stale_revisions_and_trash();
    void page_move_preserves_typed_values_and_references();
    void page_move_property_conflict_rolls_back();
    void outgoing_links_keep_flow_order_context_and_unresolved_names();
    void version_five_migration_keeps_legacy_labels_and_reads_new_modes();
    void creating_reference_target_commits_page_and_source_together();
    void failed_reference_creation_leaves_no_page_or_partial_binding();
    void managed_import_survives_original_removal_and_reopen();
    void failed_or_unsafe_import_preserves_page_and_source();
    void attachment_audit_retains_trash_recovery_and_cleans_orphans();
    void missing_or_corrupt_files_fail_backup_without_partial_output();
    void attachment_manifest_restores_and_rejects_tampered_inputs();
    void file_locks_protect_active_backups_and_imports_from_cleanup();
    void version_six_migration_preserves_old_cards_and_recovery();
    void registered_files_removed_inside_a_block_remain_available_for_undo();
    void search_indexes_properties_blocks_scope_and_trash();
    void search_languages_literals_and_prefixes();
    void search_tracks_reference_labels_moves_and_removals();
    void search_failure_rolls_back_and_rebuild_repairs();
    void version_seven_search_migration_and_backup_restore();
    void failed_search_migration_preserves_original_data();
    void reference_queries_use_ids_and_current_block_owners_without_fts();
    void saved_reference_views_combine_properties_and_survive_reopen();
    void version_eight_preserves_legacy_views_and_rejects_invalid_link_filters();
};
void StorageTests::dates_and_types()
{
    QVERIFY(valid_date("2024-02-29")); QVERIFY(!valid_date("2023-02-29"));
    QVERIFY(!valid_date("1900-02-29")); QVERIFY(valid_date("2000-02-29"));
    QVERIFY(!valid_date("0000-01-01")); QVERIFY(!valid_date("2024-13-01"));
    QVERIFY(!valid_date("2024-01-00")); QVERIFY(!valid_date("2024-1-01"));
    validate_value(PropertyKind::Date,Date{"2026-10-29"});
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,validate_value(PropertyKind::Number,std::string("2")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,validate_value(PropertyKind::Number,std::numeric_limits<double>::infinity()));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,validate_value(PropertyKind::Date,Date{"2023-02-29"}));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,validate_value(PropertyKind::Tags,std::vector<std::string>{"a","a"}));
}
void StorageTests::typed_filter_and_sort()
{
    PageRecord a{{"a",{}},"db","Ten",{{"n",10.0},{"tags",std::vector<std::string>{"work","diary"}}},0,false};
    PageRecord b{{"b",{}},"db","Two",{{"n",2.0}},0,false};
    View view; view.sort_property="n";
    auto rows=apply_view({a,b},view); QCOMPARE(rows[0].document.id,std::string("b"));
    view.filters={{"n",Comparison::Greater,5.0}};
    rows=apply_view({a,b},view); QCOMPARE(rows.size(),std::size_t(1)); QCOMPARE(rows[0].title,std::string("Ten"));
    QVERIFY(matches(a.properties["tags"],{"tags",Comparison::Contains,std::string("diary")}));
    QVERIFY(!matches(b.properties["n"],{"n",Comparison::Equal,std::string("2")}));
    QVERIFY(matches(PropertyValue{},{"n",Comparison::Empty,{}}));
}
void StorageTests::payload_roundtrip_and_rejection()
{
    const auto page=sample_page();
    for (const auto& original:page.blocks) {
        auto decoded=original; decoded.runs.clear(); decode_block(decoded,encode_block(original));
        QCOMPARE(encode_block(decoded),encode_block(original));
    }
    for (const PropertyValue& value : {PropertyValue{},PropertyValue{std::string(u8"日本語")},PropertyValue{42.5},
            PropertyValue{Date{"2024-02-29"}},PropertyValue{true},PropertyValue{std::vector<std::string>{"x","y"}}})
        QCOMPARE(encode_value(decode_value(encode_value(value))),encode_value(value));
    Block block;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_block(block,"{\"version\":99}"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_block(block,"{\"version\":1,\"runs\":true}"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_value("garbage"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,encode_value(std::numeric_limits<double>::infinity()));
    View view;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_view(view,R"({"version":1,"visible":[],"filters":[{"property":"","comparison":0,"value":{"version":99,"type":1,"value":"x"}}],"sort":"","ascending":true})"));
    block.runs={{std::string(1,char(0xff))}};
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,encode_block(block));
}
void StorageTests::upgrade_v1_preserves_identity()
{
    QTemporaryDir folder;
    sql(folder.filePath("nebula.sqlite3"),"PRAGMA application_id=0x4e454255; PRAGMA user_version=1; CREATE TABLE workspace_meta(id INTEGER PRIMARY KEY,workspace_id TEXT); INSERT INTO workspace_meta VALUES(1,'old-workspace')");
    auto workspace=Workspace::open(path(folder.path()));
    QCOMPARE(workspace.id(),std::string("old-workspace"));
    const auto db=workspace.create_database("Journal");
    QVERIFY(!workspace.create_page(db.id,"Day one").document.id.empty());
}
void StorageTests::failed_migration_rolls_back()
{
    QTemporaryDir folder;
    const auto file=folder.filePath("nebula.sqlite3");
    sql(file,"PRAGMA application_id=0x4e454255; PRAGMA user_version=1; CREATE TABLE workspace_meta(id INTEGER PRIMARY KEY,workspace_id TEXT); INSERT INTO workspace_meta VALUES(1,'old'); CREATE TABLE databases(keep TEXT); INSERT INTO databases VALUES('preserve')");
    const auto before=bytes(file);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::open(path(folder.path())));
    QCOMPARE(bytes(file),before);
}
void StorageTests::pages_and_properties_survive_reopen()
{
    QTemporaryDir folder; std::string page_id,db_id,number;
    {
        auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Journal"); db_id=db.id;
        const auto n=w.add_property(db.id,"Score",PropertyKind::Number); number=n.id;
        const auto date=w.add_property(db.id,"Entry date",PropertyKind::Date);
        auto p=w.create_page(db.id,u8"記録"); page_id=p.document.id;
        p.document=sample_page(); p.document.id=page_id;
        p.properties[n.id]=12.5; p.properties[date.id]=Date{"2026-10-29"}; w.save_page(p);
        QCOMPARE(p.revision,std::int64_t(2));
    }
    auto w=Workspace::open(path(folder.path())); auto p=w.load_page(page_id);
    QCOMPARE(p.title,std::string(u8"記録")); QCOMPARE(p.database_id,db_id); QCOMPARE(p.document.blocks.size(),std::size_t(9));
    QCOMPARE(std::get<double>(p.properties.at(number)),12.5);
    for (std::size_t i=0; i<p.document.blocks.size(); ++i)
        QCOMPARE(encode_block(p.document.blocks[i]),encode_block(sample_page().blocks[i]));
}
void StorageTests::invalid_save_rolls_back_everything()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Notes");
    auto p=w.create_page(db.id,"Keep title"); auto other=w.create_page(db.id,"Other");
    const auto n=w.add_property(db.id,"Number",PropertyKind::Number);
    p.title="Must roll back"; p.properties[n.id]=3.0; p.document.blocks[0].id=other.document.blocks[0].id;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.save_page(p));
    auto saved=w.load_page(p.document.id); QCOMPARE(saved.title,std::string("Keep title"));
    QCOMPARE(saved.revision,std::int64_t(1)); QVERIFY(saved.properties.empty()); QCOMPARE(p.revision,std::int64_t(1));
    QVERIFY(w.deleted_blocks().empty());
    saved.properties[n.id]=std::string("wrong");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.save_page(saved));
    saved.properties.clear(); saved.document.blocks[0].parent_id="missing";
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.save_page(saved));
    const auto count=w.pages(db.id).size();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.create_page(db.id,"Invalid",{{n.id,std::string("wrong")}}));
    QCOMPARE(w.pages(db.id).size(),count);
}
void StorageTests::stale_revision_cannot_overwrite_newer_edits()
{
    QTemporaryDir folder; auto a=Workspace::open(path(folder.path())); const auto db=a.create_database("Notes");
    auto first=a.create_page(db.id,"Original"); auto b=Workspace::open(path(folder.path())); auto stale=b.load_page(first.document.id);
    first.title="Latest"; a.save_page(first); stale.title="Stale";
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,b.save_page(stale));
    QCOMPARE(a.load_page(first.document.id).title,std::string("Latest"));
}
void StorageTests::views_share_pages_and_survive_reopen()
{
    QTemporaryDir folder; std::string database;
    {
        auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Journal"); database=db.id;
        auto n=w.add_property(db.id,"Number",PropertyKind::Number);
        auto a=w.create_page(db.id,"Two"); a.properties[n.id]=2.0; w.save_page(a);
        auto b=w.create_page(db.id,"Ten"); b.properties[n.id]=10.0; w.save_page(b);
        View all; all.database_id=db.id; all.name="All"; all.visible_properties={n.id}; all.sort_property=n.id; w.save_view(all);
        View filtered=all; filtered.id.clear(); filtered.name="Large"; filtered.filters={{n.id,Comparison::Greater,5.0}}; w.save_view(filtered);
    }
    auto w=Workspace::open(path(folder.path())); auto views=w.views(database); QCOMPARE(views.size(),std::size_t(2));
    auto all=apply_view(w.pages(database),views[0]); QCOMPARE(all.size(),std::size_t(2)); QCOMPARE(all[0].title,std::string("Two"));
    auto filtered=apply_view(w.pages(database),views[1]); QCOMPARE(filtered.size(),std::size_t(1)); QCOMPARE(filtered[0].document.id,all[1].document.id);
    views[1].filters[0].value=std::string("wrong");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.save_view(views[1]));
}
void StorageTests::trash_restore_and_link_rebuild()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Journal");
    auto target=w.create_page(db.id,"Target"); auto source=w.create_page(db.id,"Source");
    source.document.blocks[0].runs={{"target",false,false,Reference{ReferenceKind::Block,target.document.blocks[0].id}}}; w.save_page(source);
    QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(1));
    target.title="Renamed"; w.save_page(target); w.trash_page(target.document.id,true);
    QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(1));
    QCOMPARE(w.pages(db.id).size(),std::size_t(1)); w.trash_page(target.document.id,false);
    QCOMPARE(w.pages(db.id).size(),std::size_t(2));
    sql(folder.filePath("nebula.sqlite3"),"DELETE FROM links"); QVERIFY(w.backlinks(ReferenceKind::Page,target.document.id).empty());
    w.rebuild_links(); QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(1));
    w.trash_database(db.id,true); QVERIFY(w.databases().empty()); QVERIFY(w.pages(db.id).empty());
    w.trash_database(db.id,false); QCOMPARE(w.pages(db.id).size(),std::size_t(2));
}
void StorageTests::backup_restore_preserves_documents_and_files()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("original"))); auto db=w.create_database("Notes");
    auto p=w.create_page(db.id,"Keep me");
    const auto id=p.document.id; p.document=sample_page(); p.document.id=id;
    const auto score=w.add_property(db.id,"Score",PropertyKind::Number); p.properties[score.id]=42.5; w.save_page(p);
    QFile file(folder.filePath("original/attachments/example.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("asset bytes"); file.close();
    w.backup_to(path(folder.filePath("backup")));
    auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    QCOMPARE(restored.id(),w.id()); QCOMPARE(restored.load_page(p.document.id).title,std::string("Keep me"));
    const auto copy=restored.load_page(p.document.id);
    QCOMPARE(std::get<double>(copy.properties.at(score.id)),42.5);
    for (std::size_t i=0;i<p.document.blocks.size();++i) QCOMPARE(encode_block(copy.document.blocks[i]),encode_block(p.document.blocks[i]));
    QCOMPARE(restored.backlinks(ReferenceKind::Page,p.document.id).size(),std::size_t(1));
    QCOMPARE(bytes(folder.filePath("restored/attachments/example.txt")),QByteArray("asset bytes"));
}
void StorageTests::backup_refuses_overwrite_and_unsafe_files()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("original")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.backup_to(path(folder.filePath("original/inside"))));
    std::filesystem::create_directory(path(folder.filePath("existing")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.backup_to(path(folder.filePath("existing"))));
    QFile file(folder.filePath("external")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
    QVERIFY(QFile::link(file.fileName(),folder.filePath("original/attachments/link")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.backup_to(path(folder.filePath("failed"))));
    QVERIFY(!QFile::exists(folder.filePath("failed/nebula.sqlite3"))); QCOMPARE(bytes(file.fileName()),QByteArray("keep"));
}
void StorageTests::session_retains_failed_edits_and_blocks_navigation()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto p=w.create_page(db.id,"Initial"); auto next=w.create_page(db.id,"Next");
    PageSession session(w); session.open(p.document.id); session.set_title("Pending");
    QVERIFY(session.dirty()); QVERIFY(session.save()); QVERIFY(!session.dirty());
    auto latest=w.load_page(p.document.id); latest.title="External"; w.save_page(latest);
    session.set_title("Keep these unsaved edits"); QVERIFY(!session.save()); QVERIFY(session.dirty());
    QVERIFY(!session.error().empty()); QCOMPARE(session.record().title,std::string("Keep these unsaved edits"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,session.open(next.document.id));
    QCOMPARE(session.record().document.id,p.document.id); QCOMPARE(w.load_page(p.document.id).title,std::string("External"));
}
void StorageTests::command_line_backup_and_restore()
{
    QTemporaryDir root;
    const auto source=root.filePath("source");
    std::string id;
    { auto w=Workspace::open(path(source)); auto db=w.create_database("Notes"); id=w.create_page(db.id,"Persist across tools").document.id; }
    const auto original=bytes(source+"/nebula.sqlite3");
    QVERIFY(QFile::setPermissions(source+"/nebula.sqlite3",QFileDevice::ReadOwner));
    for (const auto& command : {QString("backup"),QString("restore")}) {
        QProcess process;
        process.start(QCoreApplication::applicationDirPath()+"/nebula_workspace",{command,
            command=="backup"?source:root.filePath("backup"),root.filePath(command=="backup"?"backup":"restored")});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
    }
    auto restored=Workspace::open(path(root.filePath("restored")));
    QCOMPARE(restored.load_page(id).title,std::string("Persist across tools"));
    QCOMPARE(bytes(source+"/nebula.sqlite3"),original);
}
void StorageTests::killed_process_before_and_after_commit()
{
    for (const auto& mode:{QString("pending"),QString("commit")}) {
        QTemporaryDir folder; std::string id;
        { auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Notes"); id=w.create_page(db.id,"Before process exit").document.id; }
        QProcess process; process.start(QCoreApplication::applicationDirPath()+"/recovery_writer",{folder.filePath("nebula.sqlite3"),mode});
        QVERIFY(process.waitForReadyRead(5000)); QCOMPARE(process.readAllStandardOutput(),QByteArray("ready\n"));
        process.kill(); QVERIFY(process.waitForFinished(5000));
        auto w=Workspace::open(path(folder.path())); const auto page=w.load_page(id); validate(page.document);
        QCOMPARE(page.title,mode=="commit"?std::string("After process exit"):std::string("Before process exit"));
        QCOMPARE(page.revision,mode=="commit"?std::int64_t(2):std::int64_t(1));
        QCOMPARE(count(folder.filePath("nebula.sqlite3"),"SELECT count(*) FROM maintenance WHERE name='interrupted_marker'"),mode=="commit"?1:0);
    }
}
void StorageTests::unresolved_references_never_bind_by_title()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("source"))); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Source"); page.document.blocks[0].runs={{"A future page",true,false,Reference{ReferenceKind::Page,"","Future",db.id}}};
    w.save_page(page); w.rebuild_links(); auto same_name=w.create_page(db.id,"Future");
    const auto ref=w.load_page(page.document.id).document.blocks[0].runs[0].reference;
    QVERIFY(ref); QVERIFY(ref->target_id.empty()); QCOMPARE(ref->unresolved_name,std::string("Future")); QCOMPARE(ref->database_scope,db.id);
    QVERIFY(w.backlinks(ReferenceKind::Page,same_name.document.id).empty()); QVERIFY(w.backlinks(ReferenceKind::Page,"").empty());
    const auto encoded=encode_document(page.document); QCOMPARE(encode_document(decode_document(encoded)),encoded);
    w.backup_to(path(folder.filePath("backup"))); auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    QCOMPARE(restored.load_page(page.document.id).document.blocks[0].runs[0].reference->unresolved_name,std::string("Future"));
}
void StorageTests::deleted_subtrees_survive_restart_and_restore()
{
    QTemporaryDir folder; std::string id,group; Page original;
    {
        auto w=Workspace::open(path(folder.filePath("source"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Restore subtree",{},sample_page().blocks); id=page.document.id;
        original=page.document;
        page.document.blocks.erase(page.document.blocks.begin()+2,page.document.blocks.begin()+4); w.save_page(page);
        const auto groups=w.deleted_blocks(); QCOMPARE(groups.size(),std::size_t(1)); group=groups[0].id;
    }
    auto w=Workspace::open(path(folder.filePath("source"))); const auto restored=w.restore_deleted_blocks(group);
    QCOMPARE(encode_document(restored.document),encode_document(original)); QVERIFY(w.deleted_blocks().empty());
    QCOMPARE(w.backlinks(ReferenceKind::Page,id).size(),std::size_t(1));
    auto p=w.load_page(id); p.document.blocks.erase(p.document.blocks.begin()+7); w.save_page(p);
    const auto image_group=w.deleted_blocks()[0].id;
    w.backup_to(path(folder.filePath("backup")));
    // A backup is restored into a new folder; opening it in place is refused (SUG-11).
    const auto manifest=bytes(folder.filePath("backup/backup-manifest.json")), database=bytes(folder.filePath("backup/nebula.sqlite3"));
    QVERIFY_THROWS_EXCEPTION(StorageError,Workspace::open(path(folder.filePath("backup"))));
    QCOMPARE(bytes(folder.filePath("backup/nebula.sqlite3")),database); QCOMPARE(bytes(folder.filePath("backup/backup-manifest.json")),manifest);
    auto backup=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    const auto image=backup.restore_deleted_blocks(image_group);
    QCOMPARE(image.document.blocks[7].asset_label,std::string("Sample image representation"));
}
void StorageTests::partial_undo_then_restore_preserves_existing_edits()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Partial undo",{},{{"a","",BlockKind::Paragraph,{{"Parent"}}},{"b","a",BlockKind::Paragraph,{{"Child"}}},{"c","a",BlockKind::Paragraph,{{"Last child"}}},{"d","",BlockKind::Paragraph,{{"Keep"}}}});
    const auto original=page.document; page.document.blocks.erase(page.document.blocks.begin(),page.document.blocks.begin()+3); w.save_page(page);
    const auto group=w.deleted_blocks()[0].id;
    page.document.blocks.insert(page.document.blocks.begin(),original.blocks.begin(),original.blocks.begin()+2); page.document.blocks[1].runs[0].text="Edited restored child"; w.save_page(page);
    QCOMPARE(w.deleted_blocks().size(),std::size_t(1)); const auto restored=w.restore_deleted_blocks(group);
    QCOMPARE(restored.document.blocks.size(),std::size_t(4)); QCOMPARE(restored.document.blocks[1].runs[0].text,std::string("Edited restored child"));
    QCOMPARE(restored.document.blocks[2].id,std::string("c")); QVERIFY(w.deleted_blocks().empty());
}
void StorageTests::restore_requires_the_deleted_parent_and_page()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Dependencies",{},{{"a","",BlockKind::Paragraph,{{"Parent"}}},{"b","a",BlockKind::Paragraph,{{"Child"}}},{"c","",BlockKind::Paragraph,{{"Keep"}}}});
    page.document.blocks.erase(page.document.blocks.begin()+1); w.save_page(page); const auto child=w.deleted_blocks()[0].id;
    page.document.blocks.erase(page.document.blocks.begin()); w.save_page(page); const auto parent=w.deleted_blocks()[1].id;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.restore_deleted_blocks(child));
    w.trash_page(page.document.id,true); QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.restore_deleted_blocks(parent)); w.trash_page(page.document.id,false);
    w.restore_deleted_blocks(parent); const auto restored=w.restore_deleted_blocks(child);
    QCOMPARE(restored.document.blocks[1].parent_id,std::string("a"));
}
void StorageTests::version_two_upgrade_preserves_notes()
{
    QTemporaryDir folder; std::string id;
    { auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); id=w.create_page(db.id,"Older notes").document.id; }
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE deleted_blocks; DROP TABLE attachments; DROP TABLE page_search; PRAGMA user_version=2");
    auto w=Workspace::open(path(folder.path())); QCOMPARE(w.load_page(id).title,std::string("Older notes")); QVERIFY(w.deleted_blocks().empty());
}
void StorageTests::restoring_siblings_in_reverse_order_keeps_their_order()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Order",{},{{"a","",BlockKind::Paragraph,{{"A"}}},{"b","",BlockKind::Paragraph,{{"B"}}},{"c","",BlockKind::Paragraph,{{"C"}}}});
    page.document.blocks.erase(page.document.blocks.begin(),page.document.blocks.begin()+2); w.save_page(page);
    const auto groups=w.deleted_blocks(); QCOMPARE(groups.size(),std::size_t(2));
    w.restore_deleted_blocks(groups[1].id); const auto restored=w.restore_deleted_blocks(groups[0].id);
    QCOMPARE(restored.document.blocks[0].id,std::string("a")); QCOMPARE(restored.document.blocks[1].id,std::string("b"));
}
void StorageTests::version_three_upgrade_preserves_deleted_fragments()
{
    QTemporaryDir folder; std::string page_id,group_id,workspace_id;
    {
        auto w=Workspace::open(path(folder.path())); workspace_id=w.id(); const auto db=w.create_database("Notes");
        auto page=w.create_page(db.id,"Legacy recovery",{},{{"a","",BlockKind::Paragraph,{{"Keep deleted text",true}}},{"b","",BlockKind::Paragraph,{{"Next sibling"}}}});
        page_id=page.document.id; page.document.blocks.erase(page.document.blocks.begin()); w.save_page(page);
        group_id=w.deleted_blocks()[0].id;
    }
    sql(folder.filePath("nebula.sqlite3"),R"SQL(
        BEGIN;
        CREATE TABLE legacy_deleted_blocks(id TEXT PRIMARY KEY NOT NULL,
            page_id TEXT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
            parent_id TEXT NOT NULL, anchor_id TEXT NOT NULL, fragment TEXT NOT NULL,
            restored INTEGER NOT NULL DEFAULT 0 CHECK(restored IN(0,1)),
            removed_at TEXT NOT NULL DEFAULT(strftime('%Y-%m-%dT%H:%M:%fZ','now')));
        INSERT INTO legacy_deleted_blocks SELECT id,page_id,parent_id,anchor_id,fragment,restored,removed_at FROM deleted_blocks;
        DROP TABLE deleted_blocks;
        ALTER TABLE legacy_deleted_blocks RENAME TO deleted_blocks;
        CREATE INDEX deleted_blocks_page ON deleted_blocks(page_id,restored);
        DROP TABLE attachments; DROP TABLE page_search; PRAGMA user_version=3;
        COMMIT;
    )SQL");
    auto w=Workspace::open(path(folder.path())); QCOMPARE(w.id(),workspace_id);
    QCOMPARE(w.deleted_blocks()[0].id,group_id);
    const auto restored=w.restore_deleted_blocks(group_id); QCOMPARE(restored.document.id,page_id);
    QCOMPARE(restored.document.blocks[0].id,std::string("a")); QCOMPARE(restored.document.blocks[1].id,std::string("b"));
    QCOMPARE(restored.document.blocks[0].runs[0].text,std::string("Keep deleted text")); QVERIFY(restored.document.blocks[0].runs[0].bold);
    QVERIFY(w.deleted_blocks().empty());
}
void StorageTests::partially_restored_sibling_still_anchors_recovery()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Partial sibling",{},{{"a","",BlockKind::Paragraph,{{"A"}}},{"b","",BlockKind::Paragraph,{{"B"}}},{"child","b",BlockKind::Paragraph,{{"Child"}}},{"c","",BlockKind::Paragraph,{{"C"}}}});
    const auto original=page.document; page.document.blocks.erase(page.document.blocks.begin(),page.document.blocks.begin()+3); w.save_page(page);
    const auto groups=w.deleted_blocks(); QCOMPARE(groups.size(),std::size_t(2));
    page.document.blocks.insert(page.document.blocks.begin(),original.blocks[1]);
    page.document.blocks[0].runs[0].text="B edited after partial undo"; w.save_page(page);
    const auto restored=w.restore_deleted_blocks(groups[0].id);
    QCOMPARE(restored.document.blocks[0].id,std::string("a")); QCOMPARE(restored.document.blocks[1].id,std::string("b"));
    QCOMPARE(restored.document.blocks[1].runs[0].text,std::string("B edited after partial undo"));
    QCOMPARE(w.deleted_blocks().size(),std::size_t(1));
}
void StorageTests::divider_migration_preserves_tree_links_and_recovery()
{
    QTemporaryDir folder; std::string id,group;
    {
        auto w=Workspace::open(path(folder.filePath("source"))); const auto db=w.create_database("Notes");
        auto page=w.create_page(db.id,"Before divider migration",{},sample_page().blocks); id=page.document.id;
        page.document.blocks.erase(page.document.blocks.begin()+7); w.save_page(page); group=w.deleted_blocks()[0].id;
    }
    sql(folder.filePath("source/nebula.sqlite3"),"DROP TABLE attachments; DROP TABLE page_search; PRAGMA user_version=4");
    auto w=Workspace::open(path(folder.filePath("source"))); auto page=w.load_page(id);
    QCOMPARE(page.document.blocks[3].parent_id,std::string("list")); QCOMPARE(w.backlinks(ReferenceKind::Block,"heading").size(),std::size_t(1));
    page.document.blocks.push_back({"divider","",BlockKind::Divider,{}}); w.save_page(page);
    QCOMPARE(w.load_page(id).document.blocks.back().kind,BlockKind::Divider);
    const auto restored=w.restore_deleted_blocks(group); QCOMPARE(restored.document.blocks[7].id,std::string("image"));
    w.backup_to(path(folder.filePath("backup"))); auto copy=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    QCOMPARE(copy.load_page(id).document.blocks.back().kind,BlockKind::Divider);
    QCOMPARE(copy.backlinks(ReferenceKind::Block,"heading").size(),std::size_t(1));
}
void StorageTests::reference_lifecycle_follows_ids_and_current_owners()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path()));
    const auto a=w.create_database("Notes"), b=w.create_database("Archive");
    auto target=w.create_page(a.id,"Duplicate",{},{{"heading","",BlockKind::Heading,{{"Original heading"}}}});
    const auto duplicate=w.create_page(a.id,"Duplicate");
    auto source=w.create_page(b.id,"Source",{},{{"source","",BlockKind::Paragraph,{
        {"Page",false,false,Reference{ReferenceKind::Page,target.document.id}},
        {"Heading",false,false,Reference{ReferenceKind::Block,"heading"}}}}});
    const Reference ref{ReferenceKind::Block,"heading"}, page_ref{ReferenceKind::Page,target.document.id};
    target.title="Renamed"; target.document.blocks[0].runs[0].text=u8"見出し changed"; w.save_page(target);
    w.move_page(target,b.id);
    QCOMPARE(w.resolve_reference(ref).page_id,target.document.id);
    QCOMPARE(w.resolve_reference(page_ref).state,ReferenceState::Available);
    QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(2));
    w.trash_page(target.document.id,true);
    QCOMPARE(w.resolve_reference(ref).state,ReferenceState::PageTrashed);
    QCOMPARE(w.resolve_reference(page_ref).state,ReferenceState::PageTrashed);
    const auto reused=w.create_page(a.id,"Renamed");
    QCOMPARE(w.resolve_reference(ref).page_id,target.document.id);
    QVERIFY(w.backlinks(ReferenceKind::Page,reused.document.id).empty());
    QVERIFY(w.backlinks(ReferenceKind::Page,duplicate.document.id).empty());
    w.trash_page(target.document.id,false); w.trash_database(b.id,true);
    QCOMPARE(w.resolve_reference(ref).state,ReferenceState::DatabaseTrashed);
    QCOMPARE(w.resolve_reference(page_ref).state,ReferenceState::DatabaseTrashed);
    w.trash_database(b.id,false);
    target=w.load_page(target.document.id); target.document.blocks.push_back({"keep","",BlockKind::Paragraph,{}});
    target.document.blocks.erase(target.document.blocks.begin()); w.save_page(target);
    QCOMPARE(w.resolve_reference(ref).state,ReferenceState::Unavailable);
    target=w.restore_deleted_blocks(w.deleted_blocks()[0].id);
    QCOMPARE(w.resolve_reference(ref).state,ReferenceState::Available);
    QCOMPARE(w.resolve_reference({ReferenceKind::Page,"unknown"}).state,ReferenceState::Unavailable);
    QCOMPARE(w.resolve_reference({ReferenceKind::Page,"","Renamed",a.id}).state,ReferenceState::Unresolved);
    QCOMPARE(w.load_page(source.document.id).document.blocks[0].runs[0].reference->target_id,target.document.id);
    w.rebuild_links(); QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(2));
}
void StorageTests::transfer_undo_redo_survives_reopen_and_backup()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    const auto source=w.create_page(db.id,"Source",{},{{"heading","",BlockKind::Heading,{{"Heading"}}},
        {"child","heading",BlockKind::Paragraph,{{"Self",true,false,Reference{ReferenceKind::Block,"child"}}}}});
    const auto destination=w.create_page(db.id,"Destination");
    const auto other=w.create_page(db.id,"Other",{},{{"other","",BlockKind::Paragraph,{{"Jump",false,true,Reference{ReferenceKind::Block,"heading"}}}}});
    auto transfer=w.transfer_blocks(source,destination,"heading");
    const auto placeholder=w.load_page(source.document.id).document.blocks[0].id;
    QVERIFY(placeholder!="heading"); QVERIFY(w.deleted_blocks().empty());
    auto check_moved=[&](Workspace& workspace) {
        QCOMPARE(workspace.resolve_reference({ReferenceKind::Block,"heading"}).page_id,destination.document.id);
        QCOMPARE(workspace.resolve_reference({ReferenceKind::Block,"child"}).page_id,destination.document.id);
        const auto moved=workspace.load_page(destination.document.id);
        QCOMPARE(moved.document.blocks[2].parent_id,std::string("heading"));
        QVERIFY(moved.document.blocks[2].runs[0].bold);
        QCOMPARE(workspace.backlinks(ReferenceKind::Page,source.document.id).size(),std::size_t(0));
        QCOMPARE(workspace.backlinks(ReferenceKind::Page,destination.document.id).size(),std::size_t(2));
        const auto backlinks=workspace.backlinks(ReferenceKind::Block,"child");
        QCOMPARE(backlinks[0].source_page,destination.document.id);
        QCOMPARE(workspace.load_page(other.document.id).document.blocks[0].runs[0].reference->target_id,std::string("heading"));
    };
    check_moved(w); w.rebuild_links(); check_moved(w);
    auto reopened=Workspace::open(path(folder.filePath("workspace"))); check_moved(reopened);
    reopened.set_transfer_applied(transfer,false);
    QCOMPARE(encode_document(reopened.load_page(source.document.id).document),encode_document(source.document));
    QCOMPARE(encode_document(reopened.load_page(destination.document.id).document),encode_document(destination.document));
    QCOMPARE(reopened.resolve_reference({ReferenceKind::Block,"child"}).page_id,source.document.id);
    reopened.set_transfer_applied(transfer,true); check_moved(reopened);
    QCOMPARE(reopened.load_page(source.document.id).document.blocks[0].id,placeholder);
    QVERIFY(reopened.deleted_blocks().empty());
    reopened.backup_to(path(folder.filePath("backup")));
    auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored"))); check_moved(restored);
}
void StorageTests::transfer_failure_rolls_back_both_pages_and_index()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto source=w.create_page(db.id,"Source",{},{{"root","",BlockKind::Paragraph,{{"Jump",false,false,Reference{ReferenceKind::Block,"child"}}}},
        {"child","root",BlockKind::Paragraph,{{"Child"}}}});
    const auto destination=w.create_page(db.id,"Destination");
    // Nebula connections run with triggers disabled; a missing derived table fails the write instead.
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search;");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.transfer_blocks(source,destination,"root"));
    QCOMPARE(encode_document(w.load_page(source.document.id).document),encode_document(source.document));
    QCOMPARE(encode_document(w.load_page(destination.document.id).document),encode_document(destination.document));
    QCOMPARE(w.load_page(source.document.id).revision,source.revision);
    QCOMPARE(w.load_page(destination.document.id).revision,destination.revision);
    QCOMPARE(w.backlinks(ReferenceKind::Block,"child")[0].source_page,source.document.id);
    QVERIFY(w.deleted_blocks().empty());
    w.rebuild_search();
    auto transfer=w.transfer_blocks(source,destination,"root");
    const auto revision=transfer.source_revision;
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search;");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.set_transfer_applied(transfer,false));
    QVERIFY(transfer.applied); QCOMPARE(transfer.source_revision,revision);
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,"child"}).page_id,destination.document.id);
    QCOMPARE(w.backlinks(ReferenceKind::Block,"child")[0].source_page,destination.document.id);
}
void StorageTests::transfer_rejects_stale_revisions_and_trash()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto source=w.create_page(db.id,"Source"), destination=w.create_page(db.id,"Destination");
    const auto stale_source=source; source.title="New title"; w.save_page(source);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.transfer_blocks(stale_source,destination,source.document.blocks[0].id));
    const auto stale_destination=destination; destination.title="Changed"; w.save_page(destination);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.transfer_blocks(source,stale_destination,source.document.blocks[0].id));
    QCOMPARE(w.load_page(source.document.id).revision,source.revision);
    w.trash_page(destination.document.id,true);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.transfer_blocks(source,w.load_page(destination.document.id),source.document.blocks[0].id));
    w.trash_page(destination.document.id,false); destination=w.load_page(destination.document.id);
    w.trash_database(db.id,true);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.transfer_blocks(source,destination,source.document.blocks[0].id));
    w.trash_database(db.id,false);
    auto transfer=w.transfer_blocks(source,destination,source.document.blocks[0].id);
    auto edited=w.load_page(destination.document.id); edited.document.blocks.back().runs={{"New edit"}}; w.save_page(edited);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.set_transfer_applied(transfer,false));
    QCOMPARE(w.load_page(source.document.id).revision,transfer.source_revision);
    QCOMPARE(w.load_page(destination.document.id).document.blocks.back().runs[0].text,std::string("New edit"));
}
void StorageTests::page_move_preserves_typed_values_and_references()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Source"), b=w.create_database("Destination");
    const auto text=w.add_property(a.id,"Text",PropertyKind::Text), number=w.add_property(a.id,"Number",PropertyKind::Number);
    const auto date=w.add_property(a.id,"Entry date",PropertyKind::Date), check=w.add_property(a.id,"Done",PropertyKind::Checkbox);
    const auto tags=w.add_property(a.id,"Tags",PropertyKind::Tags), empty=w.add_property(a.id,"Unset",PropertyKind::Text);
    const auto target_date=w.add_property(b.id,"Entry date",PropertyKind::Date);
    auto page=w.create_page(a.id,"Keep identity",{{text.id,std::string(u8"日本語")},{number.id,42.5},{date.id,Date{"2024-02-29"}},
        {check.id,false},{tags.id,std::vector<std::string>{"diary","work"}},{empty.id,{}}});
    const auto original=page;
    const auto referring=w.create_page(a.id,"Referring",{},{{"referring","",BlockKind::Paragraph,{{"Jump",false,false,Reference{ReferenceKind::Block,page.document.blocks[0].id}}}}});
    w.move_page(page,b.id); QCOMPARE(page.document.id,original.document.id); QCOMPARE(page.database_id,b.id);
    QCOMPARE(encode_document(page.document),encode_document(original.document));
    QCOMPARE(std::get<Date>(page.properties.at(target_date.id)).iso,std::string("2024-02-29"));
    QCOMPARE(page.properties.size(),original.properties.size()); QCOMPARE(w.properties(b.id).size(),std::size_t(6));
    for (const auto& target:w.properties(b.id)) for (const auto& source:w.properties(a.id)) if (source.name==target.name)
        QCOMPARE(encode_value(page.properties.at(target.id)),encode_value(original.properties.at(source.id)));
    auto reopened=Workspace::open(path(folder.path()));
    QCOMPARE(reopened.load_page(page.document.id).database_id,b.id);
    QCOMPARE(reopened.backlinks(ReferenceKind::Page,page.document.id)[0].source_page,referring.document.id);
    w.move_page(page,a.id); QCOMPARE(page.properties.size(),original.properties.size());
    for (const auto& value:original.properties) QCOMPARE(encode_value(page.properties.at(value.first)),encode_value(value.second));
}
void StorageTests::page_move_property_conflict_rolls_back()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Source"), b=w.create_database("Destination");
    auto first=w.add_property(a.id,"First",PropertyKind::Text), second=w.add_property(a.id,"Second",PropertyKind::Number);
    // Definitions iterate by property ID in a page map; ensure one new target
    // definition is inserted before the later incompatible property is rejected.
    const auto& conflict=first.id>second.id?first:second;
    const auto incompatible=w.add_property(b.id,conflict.name,PropertyKind::Checkbox);
    auto page=w.create_page(a.id,"Keep",{{first.id,std::string("text")},{second.id,4.0}}); const auto original=page;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.move_page(page,b.id));
    QCOMPARE(page.revision,original.revision); QCOMPARE(page.database_id,a.id);
    QCOMPARE(w.load_page(page.document.id).database_id,a.id); QCOMPARE(w.load_page(page.document.id).revision,original.revision);
    QCOMPARE(w.properties(b.id).size(),std::size_t(1)); QCOMPARE(w.properties(b.id)[0].id,incompatible.id);
    w.trash_database(b.id,true); QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.move_page(page,b.id));
    auto changed=page; changed.title="Updated"; w.save_page(changed);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.move_page(page,b.id));
    QCOMPARE(w.load_page(page.document.id).title,std::string("Updated"));
}
void StorageTests::outgoing_links_keep_flow_order_context_and_unresolved_names()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto target=w.create_page(db.id,"Target");
    auto source=w.create_page(db.id,"Source",{},{{"first","",BlockKind::Paragraph,{
        {"Before "},{"Future",false,false,Reference{ReferenceKind::Page,"","Future",db.id}},
        {" then "},{"Jump",false,false,Reference{ReferenceKind::Page,target.document.id}}}},
        {"second","",BlockKind::Paragraph,{{"Heading link",false,false,Reference{ReferenceKind::Block,target.document.blocks[0].id}}}}});
    auto outgoing=w.outgoing_links(source.document.id); QCOMPARE(outgoing.size(),std::size_t(3));
    QCOMPARE(outgoing[0].unresolved_name,std::string("Future")); QCOMPARE(outgoing[0].database_scope,db.id);
    QCOMPARE(outgoing[1].label,std::string("Jump")); QCOMPARE(outgoing[1].source_context,std::string("Before Future then Jump"));
    QCOMPARE(outgoing[2].source_block,std::string("second"));
    const auto incoming=w.backlinks(ReferenceKind::Page,target.document.id); QCOMPARE(incoming.size(),std::size_t(2));
    QCOMPARE(incoming[0].source_context,std::string("Before Future then Jump"));
    sql(folder.filePath("nebula.sqlite3"),"DELETE FROM links"); QCOMPARE(w.outgoing_links(source.document.id).size(),std::size_t(1));
    w.rebuild_links(); QCOMPARE(w.outgoing_links(source.document.id).size(),std::size_t(3));
    w.trash_page(source.document.id,true); QVERIFY(w.outgoing_links(source.document.id).empty());
    QVERIFY(w.backlinks(ReferenceKind::Page,target.document.id).empty()); w.trash_page(source.document.id,false);
    source=w.load_page(source.document.id); const auto destination=w.create_page(db.id,"Destination");
    w.transfer_blocks(source,destination,"first");
    outgoing=w.outgoing_links(destination.document.id); QCOMPARE(outgoing.size(),std::size_t(2));
    QCOMPARE(outgoing[1].source_page,destination.document.id); QCOMPARE(outgoing[1].target_id,target.document.id);
}
void StorageTests::version_five_migration_keeps_legacy_labels_and_reads_new_modes()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("source"))); const auto db=w.create_database("Notes");
    const auto legacy=w.create_page(db.id,"Legacy"); const auto identity=w.id();
    sql(folder.filePath("source/nebula.sqlite3"),R"SQL(
        UPDATE blocks SET payload='{"version":1,"runs":[{"text":"Legacy label","bold":true,"italic":false,"reference":{"kind":0,"target":"legacy-target"}}],"asset_label":"","checked":false}';
        DROP TABLE attachments; DROP TABLE page_search; PRAGMA user_version=5;
    )SQL");
    auto upgraded=Workspace::open(path(folder.filePath("source"))); QCOMPARE(upgraded.id(),identity);
    auto page=upgraded.load_page(legacy.document.id); QVERIFY(page.document.blocks[0].runs[0].reference->custom_label);
    QCOMPARE(page.document.blocks[0].runs[0].text,std::string("Legacy label")); QVERIFY(page.document.blocks[0].runs[0].bold);
    page.document.blocks[0].runs[0].reference->custom_label=false; upgraded.save_page(page);
    QVERIFY(!upgraded.load_page(page.document.id).document.blocks[0].runs[0].reference->custom_label);
    upgraded.backup_to(path(folder.filePath("backup"))); auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    QVERIFY(!restored.load_page(page.document.id).document.blocks[0].runs[0].reference->custom_label);
    Block invalid;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_block(invalid,R"({"version":2,"runs":[{"text":"x","bold":false,"italic":false,"reference":{"kind":0,"target":"id"}}],"asset_label":"","checked":false})"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,decode_block(invalid,R"({"version":3,"runs":[],"asset_label":"","checked":false})"));
}
void StorageTests::creating_reference_target_commits_page_and_source_together()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto source_db=w.create_database("Notes"), target_db=w.create_database("Projects");
    auto source=w.create_page(source_db.id,"Source",{},{{"source","",BlockKind::Paragraph,{{"Future",true,false,Reference{ReferenceKind::Page,"","Future",source_db.id,false}}}}});
    const auto before=source.document;
    const auto target=w.create_reference_target(source,{"source",0,1},target_db.id,"Chosen name");
    QCOMPARE(target.database_id,target_db.id); QCOMPARE(source.document.blocks[0].runs[0].text,std::string("Chosen name"));
    QVERIFY(!source.document.blocks[0].runs[0].reference->custom_label); QVERIFY(source.document.blocks[0].runs[0].bold);
    QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(1));
    QCOMPARE(w.load_page(source.document.id).revision,source.revision);
    auto after=source.document; source.document=before; w.save_page(source);
    QVERIFY(w.backlinks(ReferenceKind::Page,target.document.id).empty()); QCOMPARE(w.pages(target_db.id).size(),std::size_t(1));
    source.document=after; w.save_page(source); QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(1));
    auto reopened=Workspace::open(path(folder.path())); QCOMPARE(reopened.load_page(source.document.id).document.blocks[0].runs[0].reference->target_id,target.document.id);
    QCOMPARE(*reopened.reference_label({ReferenceKind::Page,target.document.id}),std::string("Chosen name"));
}
void StorageTests::failed_reference_creation_leaves_no_page_or_partial_binding()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"), b=w.create_database("Projects");
    auto source=w.create_page(a.id,"Source",{},{{"source","",BlockKind::Paragraph,{{"Future",false,false,Reference{ReferenceKind::Page,"","Future",a.id,false}}}}});
    const auto original=source;
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search;");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.create_reference_target(source,{"source",0,1},b.id,"Future"));
    QCOMPARE(source.revision,original.revision); QCOMPARE(encode_document(w.load_page(source.document.id).document),encode_document(original.document));
    QVERIFY(w.pages(b.id).empty()); QVERIFY(w.deleted_blocks().empty());
    w.rebuild_search();
    auto changed=source; changed.title="New title"; w.save_page(changed);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.create_reference_target(source,{"source",0,1},b.id,"Future")); QVERIFY(w.pages(b.id).empty());
    source=w.load_page(source.document.id); w.trash_database(b.id,true);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.create_reference_target(source,{"source",0,1},b.id,"Future"));
    QVERIFY(w.pages(b.id,true).empty());
}
void StorageTests::managed_import_survives_original_removal_and_reopen()
{
    QTemporaryDir folder; const auto original=folder.filePath(QString::fromUtf8(u8"資料 notes.bin"));
    const QByteArray payload("notes\n\0binary",13); put(original,payload);
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(original),false);
    QCOMPARE(page.document.blocks.size(),std::size_t(3)); QCOMPARE(page.document.blocks[1].attachment_id,imported.file.id);
    QCOMPARE(imported.file.size,std::int64_t(payload.size())); QCOMPARE(imported.file.name,std::string(u8"資料 notes.bin"));
    QCOMPARE(bytes(QString::fromStdString(w.attachment_path(imported.file.id).u8string())),payload);
    put(original,"changed original"); QCOMPARE(bytes(QString::fromStdString(w.attachment_path(imported.file.id).u8string())),payload);
    QVERIFY(QFile::remove(original)); auto reopened=Workspace::open(path(folder.filePath("workspace")));
    QCOMPARE(reopened.load_page(page.document.id).document.blocks[1].attachment_id,imported.file.id);
    QCOMPARE(bytes(QString::fromStdString(reopened.attachment_path(imported.file.id).u8string())),payload);
    QVERIFY(reopened.audit_attachments().empty());
}
void StorageTests::failed_or_unsafe_import_preserves_page_and_source()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.bin"); put(original,"keep source");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source"); const auto before=page;
    sql(folder.filePath("workspace/nebula.sqlite3"),"DROP TABLE page_search;");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_attachment(page,page.document.blocks[0].id,path(original),false));
    QCOMPARE(page.revision,before.revision); QCOMPARE(encode_document(w.load_page(page.document.id).document),encode_document(before.document));
    QVERIFY(w.attachments().empty()); QVERIFY(w.audit_attachments().empty()); QCOMPARE(bytes(original),QByteArray("keep source"));
    w.rebuild_search();
    auto changed=page; changed.title="Changed"; w.save_page(changed);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_attachment(page,page.document.blocks[0].id,path(original),false)); QVERIFY(w.attachments().empty());
    page=w.load_page(page.document.id);
    QVERIFY(QFile::link(original,folder.filePath("symlink")));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_attachment(page,page.document.blocks[0].id,path(folder.filePath("symlink")),false));
    const auto fifo=path(folder.filePath("pipe")); QVERIFY(::mkfifo(fifo.c_str(),0600)==0);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_attachment(page,page.document.blocks[0].id,fifo,false));
    QVERIFY(w.attachments().empty()); QVERIFY(w.audit_attachments().empty());
}
void StorageTests::attachment_audit_retains_trash_recovery_and_cleans_orphans()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.txt"); put(original,"keep through recovery");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(original),false);
    w.trash_page(page.document.id,true); w.trash_database(db.id,true); QVERIFY(w.audit_attachments().empty());
    w.trash_database(db.id,false); w.trash_page(page.document.id,false); page=w.load_page(page.document.id);
    page.document.blocks.erase(page.document.blocks.begin()+1,page.document.blocks.end()); w.save_page(page);
    QVERIFY(w.audit_attachments().empty()); QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.remove_unused_attachment(imported.file.id));
    const auto groups=w.deleted_blocks(); const auto file_group=std::find_if(groups.begin(),groups.end(),[](const auto& group){return group.label=="source.txt";}); QVERIFY(file_group!=groups.end());
    page=w.restore_deleted_blocks(file_group->id); QCOMPARE(page.document.blocks[1].attachment_id,imported.file.id);
    put(folder.filePath("workspace/attachments/.import-interrupted"),"incomplete");
    auto issues=w.audit_attachments(); QCOMPARE(issues.size(),std::size_t(1)); QVERIFY(issues[0].removable);
    w.remove_unused_attachment(".import-interrupted"); QVERIFY(w.audit_attachments().empty());
    QVERIFY(QFile::exists(QString::fromStdString(w.attachment_path(imported.file.id).u8string())));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.remove_unused_attachment("../nebula.sqlite3"));
}
void StorageTests::missing_or_corrupt_files_fail_backup_without_partial_output()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.txt"); put(original,"original bytes");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(original),false);
    const auto managed=QString::fromStdString(w.attachment_path(imported.file.id).u8string()); put(managed,"changed bytes");
    auto issues=w.audit_attachments(); QVERIFY(!issues.empty()); QVERIFY(!issues[0].removable);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.backup_to(path(folder.filePath("corrupt-backup"))));
    QVERIFY(!QFile::exists(folder.filePath("corrupt-backup"))); QVERIFY(QFile::remove(managed));
    QVERIFY(!w.audit_attachments().empty()); page.title="Still editable"; w.save_page(page);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.backup_to(path(folder.filePath("missing-backup"))));
    QVERIFY(!QFile::exists(folder.filePath("missing-backup"))); put(managed,"original bytes"); QVERIFY(w.audit_attachments().empty());
    w.backup_to(path(folder.filePath("good-backup"))); QVERIFY(QFile::exists(folder.filePath("good-backup/backup-manifest.json")));
}
void StorageTests::attachment_manifest_restores_and_rejects_tampered_inputs()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.txt"); put(original,"restore me");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(original),false); QVERIFY(QFile::remove(original));
    const auto backup=folder.filePath("backup"); w.backup_to(path(backup)); const auto manifest=bytes(backup+"/backup-manifest.json");
    auto restored=Workspace::restore(path(backup),path(folder.filePath("restored")));
    QCOMPARE(restored.load_page(page.document.id).document.blocks[1].attachment_id,imported.file.id);
    QCOMPARE(bytes(QString::fromStdString(restored.attachment_path(imported.file.id).u8string())),QByteArray("restore me"));
    QCOMPARE(bytes(backup+"/backup-manifest.json"),manifest); QVERIFY(!QFile::exists(folder.filePath("restored/backup-manifest.json")));
    const auto file=backup+"/attachments/"+QString::fromStdString(imported.file.id); put(file,"tampered");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::restore(path(backup),path(folder.filePath("reject-file"))));
    QVERIFY(!QFile::exists(folder.filePath("reject-file"))); put(file,"restore me");
    auto object=QJsonDocument::fromJson(manifest).object(); auto entries=object["files"].toArray(); auto entry=entries[0].toObject(); entry["path"]="../outside"; entries[0]=entry; object["files"]=entries;
    put(backup+"/backup-manifest.json",QJsonDocument(object).toJson());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::restore(path(backup),path(folder.filePath("reject-path"))));
    QVERIFY(!QFile::exists(folder.filePath("reject-path"))); put(backup+"/backup-manifest.json",manifest);
    put(backup+"/attachments/unlisted","extra");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::restore(path(backup),path(folder.filePath("reject-extra"))));
    QVERIFY(QFile::remove(backup+"/attachments/unlisted")); QVERIFY(QFile::remove(backup+"/backup-manifest.json"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::restore(path(backup),path(folder.filePath("reject-missing-manifest"))));
}
void StorageTests::file_locks_protect_active_backups_and_imports_from_cleanup()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.txt"); put(original,"file");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    put(folder.filePath("workspace/attachments/orphan"),"unused");
    {
        files::WorkspaceLock active_backup(w.directory(),false);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.remove_unused_attachment("orphan"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_attachment(page,page.document.blocks[0].id,path(original),false));
        w.backup_to(path(folder.filePath("concurrent-read-backup")));
    }
    {
        files::WorkspaceLock active_import(w.directory(),true);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.backup_to(path(folder.filePath("busy-backup"))));
        QVERIFY(!QFile::exists(folder.filePath("busy-backup")));
    }
    w.remove_unused_attachment("orphan"); QVERIFY(w.audit_attachments().empty());
}
void StorageTests::version_six_migration_preserves_old_cards_and_recovery()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Old cards",{},{{"keep","",BlockKind::Paragraph,{{"Text"}}},{"old-card","",BlockKind::Attachment,{},"Legacy label"}});
    page.document.blocks.pop_back(); w.save_page(page); const auto group=w.deleted_blocks()[0].id; const auto identity=w.id();
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE attachments; DROP TABLE page_search; PRAGMA user_version=6;");
    auto upgraded=Workspace::open(path(folder.path())); QCOMPARE(upgraded.id(),identity); QVERIFY(upgraded.attachments().empty());
    const auto restored=upgraded.restore_deleted_blocks(group); QCOMPARE(restored.document.blocks[1].asset_label,std::string("Legacy label"));
    QVERIFY(restored.document.blocks[1].attachment_id.empty());
}
void StorageTests::registered_files_removed_inside_a_block_remain_available_for_undo()
{
    QTemporaryDir folder; const auto original=folder.filePath("source.txt"); put(original,"undo bytes");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Source");
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(original),false); const auto before=page.document;
    page.document.blocks[1].attachment_id.clear(); page.document.blocks[1].asset_label.clear(); page.document.blocks[1].kind=BlockKind::Paragraph; w.save_page(page);
    QVERIFY(w.deleted_blocks().empty());
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.remove_unused_attachment(imported.file.id));
    w.backup_to(path(folder.filePath("retained-backup")));
    auto restored=Workspace::restore(path(folder.filePath("retained-backup")),path(folder.filePath("retained-restored")));
    QCOMPARE(bytes(QString::fromStdString(restored.attachment_path(imported.file.id).u8string())),QByteArray("undo bytes"));
    QCOMPARE(bytes(QString::fromStdString(w.attachment_path(imported.file.id).u8string())),QByteArray("undo bytes"));
    page.document=before; w.save_page(page); QVERIFY(w.audit_attachments().empty());
}
void StorageTests::search_indexes_properties_blocks_scope_and_trash()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path()));
    const auto db=w.create_database("Journal"), other=w.create_database("Other");
    const auto tag=w.add_property(db.id,"Category",PropertyKind::Tags);
    const auto date=w.add_property(db.id,"Entry date",PropertyKind::Date);
    const auto score=w.add_property(db.id,"Score",PropertyKind::Number);
    const auto done=w.add_property(db.id,"Done",PropertyKind::Checkbox);
    auto page=w.create_page(db.id,"Summer",{{tag.id,std::vector<std::string>{"travel","家族"}},{date.id,Date{"2024-03-10"}},{score.id,42.5},{done.id,false}});
    page.document.blocks[0].runs={{"ocean early morning"}};
    page.document.blocks.push_back({"search-second",{},BlockKind::Paragraph,{{"volcano hiking"}}});
    w.save_page(page); w.create_page(other.id,"Ocean");
    auto results=w.search("hiking"); QCOMPARE(results.size(),std::size_t(1));
    QCOMPARE(results[0].block_id,page.document.blocks[1].id);
    QCOMPARE(w.search("ocean").size(),std::size_t(2)); QCOMPARE(w.search("ocean",db.id).size(),std::size_t(1));
    QCOMPARE(w.search("morning hiking").size(),std::size_t(1));
    for (const auto* term:{"Summer","travel","家族","2024-03-10","42.5","false"})
        QCOMPARE(w.search(term,db.id).size(),std::size_t(1));
    page.document.blocks[1].runs={{"forest walking"}}; w.save_page(page); QVERIFY(w.search("hiking").empty());
    w.trash_page(page.document.id,true); QVERIFY(w.search("forest").empty()); w.trash_page(page.document.id,false);
    QCOMPARE(w.search("forest").size(),std::size_t(1)); w.trash_database(db.id,true); QVERIFY(w.search("forest").empty());
    w.trash_database(db.id,false); const auto revision=w.load_page(page.document.id).revision;
    w.rebuild_search(); QCOMPARE(w.search("forest").size(),std::size_t(1)); QCOMPARE(w.load_page(page.document.id).revision,revision);
}
void StorageTests::search_languages_literals_and_prefixes()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Languages");
    auto page=w.create_page(db.id,"Fixtures");
    page.document.blocks[0].runs={{u8"Café ＡＢＣ 日本語の記録 中文笔记 한국어일기 العربية quoted\"word OR testing"}};
    w.save_page(page);
    for (const auto* term:{"CAFE","abc","本語","文笔","국어","العربية","quoted\"word","OR","test*"})
        QCOMPARE(w.search(term).size(),std::size_t(1));
    QVERIFY(w.search("test").empty()); QVERIFY(w.search("absent OR").empty());
    QVERIFY(w.search("\"* () 😀").empty()); QVERIFY(w.search("hiking' OR 1=1 --").empty());
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.search(std::string(4097,'a')));
    std::string many; for (int i=0;i<33;++i) many+="a ";
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.search(many));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.search(std::string(1,char(0xff))));
    page.document.blocks[0].runs={{std::string("before\0after",12)}}; w.save_page(page);
    QCOMPARE(w.search("after").size(),std::size_t(1));
    QCOMPARE(w.load_page(page.document.id).document.blocks[0].runs[0].text,std::string("before\0after",12));
}
void StorageTests::search_tracks_reference_labels_moves_and_removals()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto target=w.create_page(db.id,"Oldlabel"), source=w.create_page(db.id,"Source"), destination=w.create_page(db.id,"Destination");
    target.document.blocks[0].runs={{"Oldheading"}}; w.save_page(target);
    source.document.blocks[0].runs={{"fallback",false,false,Reference{ReferenceKind::Page,target.document.id,{},{},false}},
        {" "},{"fallbackblock",false,false,Reference{ReferenceKind::Block,target.document.blocks[0].id,{},{},false}}};
    w.save_page(source); QCOMPARE(w.search("Oldlabel").size(),std::size_t(2));
    target.title="Newlabel"; target.document.blocks[0].runs={{"Newheading"}}; w.save_page(target);
    QVERIFY(w.search("Oldlabel").empty()); QCOMPARE(w.search("Newlabel").size(),std::size_t(2));
    QCOMPARE(w.search("Newheading").size(),std::size_t(2));
    const auto transfer=w.transfer_blocks(target,destination,target.document.blocks[0].id);
    Q_UNUSED(transfer); destination=w.load_page(destination.document.id); destination.document.blocks.back().runs={{"Movedheading"}}; w.save_page(destination);
    QCOMPARE(w.search("Movedheading").size(),std::size_t(2)); QVERIFY(w.search("Newheading").empty());
    destination.document.blocks.pop_back(); w.save_page(destination);
    QVERIFY(w.search("Movedheading").empty()); QCOMPARE(w.search("fallbackblock").size(),std::size_t(1));
}
void StorageTests::search_failure_rolls_back_and_rebuild_repairs()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Original"); const auto revision=page.revision;
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search"); page.title="Pending";
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.save_page(page)); QCOMPARE(page.revision,revision);
    QCOMPARE(w.load_page(page.document.id).title,std::string("Original"));
    w.rebuild_search(); QCOMPARE(w.search("Original").size(),std::size_t(1)); w.save_page(page);
    QVERIFY(w.search("Original").empty()); QCOMPARE(w.search("Pending").size(),std::size_t(1));
}
void StorageTests::version_seven_search_migration_and_backup_restore()
{
    QTemporaryDir folder; std::string id;
    {
        auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
        id=w.create_page(db.id,"Migrated searchable").document.id;
    }
    sql(folder.filePath("workspace/nebula.sqlite3"),"DROP TABLE page_search; PRAGMA user_version=7");
    auto w=Workspace::open(path(folder.filePath("workspace"))); QCOMPARE(w.search("Migrated")[0].page_id,id);
    sql(folder.filePath("workspace/nebula.sqlite3"),"DELETE FROM page_search");
    w.backup_to(path(folder.filePath("backup")));
    auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    QCOMPARE(restored.search("Migrated")[0].page_id,id); QCOMPARE(restored.load_page(id).revision,std::int64_t(1));
}
void StorageTests::failed_search_migration_preserves_original_data()
{
    QTemporaryDir folder;
    { auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); w.create_page(db.id,"Preserve"); }
    const auto file=folder.filePath("nebula.sqlite3");
    sql(file,"DROP TABLE page_search; PRAGMA user_version=7; UPDATE blocks SET payload='unsupported'");
    const auto before=bytes(file); QVERIFY_THROWS_EXCEPTION(std::runtime_error,Workspace::open(path(folder.path())));
    QCOMPARE(bytes(file),before);
}
void StorageTests::reference_queries_use_ids_and_current_block_owners_without_fts()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"), other=w.create_database("Other");
    auto target=w.create_page(db.id,"Duplicate",{},{{"heading",{},BlockKind::Heading,{{"Heading"}}}}), destination=w.create_page(other.id,"Duplicate");
    auto first=w.create_page(db.id,"First",{},{{"first",{},BlockKind::Paragraph,{
        {"common page",false,false,Reference{ReferenceKind::Page,target.document.id}},
        {" common block",false,false,Reference{ReferenceKind::Block,"heading"}}}}});
    const auto second=w.create_page(other.id,"Second",{},{{"second",{},BlockKind::Paragraph,{{"common block",false,false,Reference{ReferenceKind::Block,"heading"}}}}});
    auto unresolved=w.create_page(db.id,"Unresolved",{},{{"unresolved",{},BlockKind::Paragraph,{{"common Duplicate",false,false,Reference{ReferenceKind::Page,{},"Duplicate"}}}}});
    const std::vector<LinkFilter> incoming{{LinkRelation::ReferencesPage,target.document.id}};
    QCOMPARE(w.search("",{},incoming).size(),std::size_t(2)); QCOMPARE(w.search("common",{},incoming).size(),std::size_t(2));
    QCOMPARE(w.search("absent",{},incoming).size(),std::size_t(0)); QCOMPARE(w.search("",db.id,incoming).size(),std::size_t(1));
    auto outgoing=w.search("",{},{{LinkRelation::ReferencedByPage,first.document.id}});
    QCOMPARE(outgoing.size(),std::size_t(1)); QCOMPARE(outgoing[0].page_id,target.document.id);
    unresolved.document.blocks[0].runs.push_back({"first",false,false,Reference{ReferenceKind::Page,first.document.id}}); w.save_page(unresolved);
    auto both=incoming; both.push_back({LinkRelation::ReferencedByPage,unresolved.document.id});
    QCOMPARE(w.search("",{},both).size(),std::size_t(1)); QCOMPARE(w.search("",{},both)[0].page_id,first.document.id);
    target.title="Renamed"; w.save_page(target); QCOMPARE(w.search("",{},incoming).size(),std::size_t(2));
    const auto transfer=w.transfer_blocks(target,destination,"heading"); Q_UNUSED(transfer);
    QCOMPARE(w.search("",{},incoming).size(),std::size_t(1)); QCOMPARE(w.search("",{},incoming)[0].page_id,first.document.id);
    QCOMPARE(w.search("",{},{{LinkRelation::ReferencesPage,destination.document.id}}).size(),std::size_t(2));
    w.trash_page(target.document.id,true); QCOMPARE(w.search("",{},incoming).size(),std::size_t(1));
    w.trash_page(first.document.id,true); QVERIFY(w.search("",{},incoming).empty()); w.trash_page(first.document.id,false);
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search");
    QCOMPARE(w.search("",{},incoming).size(),std::size_t(1)); // Structured queries do not depend on FTS.
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE links");
    w.rebuild_links(); QCOMPARE(w.search("",{},incoming)[0].page_id,first.document.id); w.rebuild_search();
    w.trash_database(other.id,true); QVERIFY(w.search("",{},{{LinkRelation::ReferencedByPage,second.document.id}}).empty());
}
void StorageTests::saved_reference_views_combine_properties_and_survive_reopen()
{
    QTemporaryDir folder; std::string database,view_id,first_id,target_id,number;
    {
        auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); database=db.id;
        const auto score=w.add_property(db.id,"Score",PropertyKind::Number); number=score.id;
        const auto target=w.create_page(db.id,"Target"); target_id=target.document.id;
        auto first=w.create_page(db.id,"First",{{score.id,8.0}},{{"first",{},BlockKind::Paragraph,{{"target",false,false,Reference{ReferenceKind::Page,target_id}}}}}); first_id=first.document.id;
        w.create_page(db.id,"Second",{{score.id,2.0}},{{"second",{},BlockKind::Paragraph,{{"target",false,false,Reference{ReferenceKind::Page,target_id}}}}});
        View view; view.database_id=db.id; view.name="Incoming high scores"; view.link_filters={{LinkRelation::ReferencesPage,target_id}};
        view.filters={{score.id,Comparison::Greater,5.0}}; w.save_view(view); view_id=view.id;
        QCOMPARE(w.query_view(view).size(),std::size_t(1)); QCOMPARE(w.query_view(view)[0].document.id,first_id);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,apply_view(w.pages(db.id),view));
    }
    auto w=Workspace::open(path(folder.path())); auto view=w.views(database)[0]; QCOMPARE(view.id,view_id);
    QCOMPARE(view.link_filters[0].page_id,target_id); QCOMPARE(w.query_view(view)[0].document.id,first_id);
    auto first=w.load_page(first_id); first.properties[number]=0.0; w.save_page(first); QVERIFY(w.query_view(view).empty());
    view.link_filters={{LinkRelation::ReferencedByPage,"missing-page"}}; w.save_view(view); QVERIFY(w.query_view(view).empty());
    QCOMPARE(w.views(database)[0].link_filters[0].page_id,std::string("missing-page"));
}
void StorageTests::version_eight_preserves_legacy_views_and_rejects_invalid_link_filters()
{
    QTemporaryDir folder; std::string database;
    {
        auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); database=db.id; w.create_page(db.id,"Keep");
        View view; view.database_id=db.id; view.name="Legacy"; w.save_view(view);
    }
    sql(folder.filePath("nebula.sqlite3"),R"(UPDATE saved_views SET settings='{"version":1,"visible":[],"filters":[],"sort":"","ascending":true}'; PRAGMA user_version=8)");
    auto w=Workspace::open(path(folder.path())); auto view=w.views(database)[0]; QVERIFY(view.link_filters.empty()); QCOMPARE(w.query_view(view).size(),std::size_t(1));
    for(const auto filters:{std::vector<LinkFilter>{{LinkRelation::ReferencesPage,{}}},
        std::vector<LinkFilter>{{LinkRelation(99),"target"}},std::vector<LinkFilter>(9,{LinkRelation::ReferencesPage,"target"})}) {
        view.link_filters=filters; QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.save_view(view));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.search("",{},filters));
    }
    QVERIFY(w.views(database)[0].link_filters.empty());
}
QTEST_GUILESS_MAIN(StorageTests)
#include "storage_tests.moc"
