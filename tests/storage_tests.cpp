#include "nebula/storage/workspace.hpp"
#include "nebula/storage/codec.hpp"
#include "nebula/app/page_session.hpp"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <sqlite3.h>
#include <limits>

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
QByteArray bytes(const QString& file) { QFile in(file); if (!in.open(QIODevice::ReadOnly)) return {}; return in.readAll(); }
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
        if (mode=="pending") QVERIFY_THROWS_EXCEPTION(std::runtime_error,sql(folder.filePath("nebula.sqlite3"),"SELECT * FROM interrupted_marker"));
    }
}
QTEST_GUILESS_MAIN(StorageTests)
#include "storage_tests.moc"
