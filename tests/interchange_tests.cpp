#include "nebula/storage/workspace.hpp"
#include "nebula/format/codec.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <sqlite3.h>
using namespace nebula;
namespace {
std::filesystem::path path(const QString& value){return std::filesystem::u8path(value.toUtf8().constData());}
void put(const QString& file,const QByteArray& bytes){QFile out(file); if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()) throw std::runtime_error("test write failed");}
QByteArray read(const QString& file){QFile in(file); if(!in.open(QIODevice::ReadOnly)) throw std::runtime_error("test read failed"); return in.readAll();}
std::string text(const Block& block){std::string result; for(const auto& run:block.runs) result+=run.text; return result;}
}
class InterchangeTests:public QObject{
    Q_OBJECT
private slots:
    void markdown_subset_remaps_pages_blocks_and_files();
    void markdown_reports_losses_and_never_fetches_resources();
    void markdown_rejection_keeps_existing_data_and_files();
    void markdown_forward_missing_and_duplicate_anchors();
    void markdown_sql_failure_and_missing_export_file_leave_no_partial_data();
    void markdown_image_inline_asset_and_unsafe_identity_roundtrip();
};
void InterchangeTests::markdown_subset_remaps_pages_blocks_and_files()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    auto target=w.create_page(db.id,"Duplicate",{},{{"heading",{},BlockKind::Heading,{{u8"日本語 café"}}}});
    auto source=w.create_page(db.id,"Duplicate",{},{{"paragraph",{},BlockKind::Paragraph,{{"Bold",true},{" italic",false,true},{u8" a\\b * [] <>\u2028next "},
        {"jump",false,false,Reference{ReferenceKind::Block,"heading"}}}},
        {"bullet",{},BlockKind::Bullet,{{"one"}}},{"todo",{},BlockKind::Todo,{{"two"}},{},true},{"divider",{},BlockKind::Divider}});
    put(folder.filePath("asset.txt"),"attachment bytes"); const auto imported=w.import_attachment(source,"divider",path(folder.filePath("asset.txt")),false);
    auto report=w.export_markdown(path(folder.filePath("markdown"))); QCOMPARE(report.pages.size(),std::size_t(2)); QVERIFY(!report.warnings.empty());
    const auto targetfile=folder.filePath("markdown/id-"+QString::fromStdString(target.document.id)+".md");
    QVERIFY(read(targetfile).contains("block-heading")); QVERIFY(QFile::remove(folder.filePath("asset.txt")));
    const auto count=w.pages(db.id).size(); report=w.import_markdown(path(folder.filePath("markdown")),db.id); QCOMPARE(report.pages.size(),std::size_t(2));
    QCOMPARE(w.pages(db.id).size(),count+2); QVERIFY(!report.warnings.empty());
    PageRecord first,second;
    for (const auto& id:report.pages) {auto p=w.load_page(id); if(p.document.blocks[0].kind==BlockKind::Heading) second=p; else first=p;}
    QVERIFY(!first.document.id.empty()); QVERIFY(!second.document.id.empty()); QVERIFY(first.document.id!=source.document.id);
    QCOMPARE(text(first.document.blocks[0]),text(source.document.blocks[0])); QVERIFY(first.document.blocks[0].runs[0].bold);
    QCOMPARE(first.document.blocks[0].runs.back().reference->target_id,second.document.blocks[0].id);
    QCOMPARE(first.document.blocks[1].kind,BlockKind::Bullet); QCOMPARE(first.document.blocks[2].kind,BlockKind::Todo); QVERIFY(first.document.blocks[2].checked);
    QCOMPARE(first.document.blocks[3].kind,BlockKind::Divider);
    const auto file=first.document.blocks[4].attachment_id; QVERIFY(!file.empty()); QVERIFY(file!=imported.file.id);
    QCOMPARE(read(QString::fromStdString(w.attachment_path(file).u8string())),QByteArray("attachment bytes"));
    QCOMPARE(w.backlinks(ReferenceKind::Page,second.document.id).size(),std::size_t(1));
    QCOMPARE(w.search("cafe").size(),std::size_t(2)); QCOMPARE(w.load_page(source.document.id).revision,source.revision);
}
void InterchangeTests::markdown_reports_losses_and_never_fetches_resources()
{
    QTemporaryDir folder; QDir root(folder.path()); QVERIFY(root.mkdir("input"));
    put(folder.filePath("input/one.md"),"# Title\n\n## Heading\n- one\n- [x] two\n\n![remote](https://invalid.example/image.png) [web](https://invalid.example)\n\n> quote\n");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    const auto report=w.import_markdown(path(folder.filePath("input")),db.id); QVERIFY(report.warnings.size()>=2); QVERIFY(w.attachments().empty());
    const auto page=w.load_page(report.pages[0]); QCOMPARE(page.document.blocks[0].kind,BlockKind::Heading); QCOMPARE(page.document.blocks[1].kind,BlockKind::Bullet);
    QCOMPARE(page.document.blocks[2].kind,BlockKind::Todo); QCOMPARE(text(page.document.blocks[3]),std::string("remote web"));
    QVERIFY(w.outgoing_links(page.document.id).empty());
    auto nested=page; nested.document.blocks[1].parent_id=nested.document.blocks[0].id; w.save_page(nested);
    const auto exported=w.export_markdown(path(folder.filePath("output"))); bool nesting=false;
    for(const auto& warning:exported.warnings) if(warning.find("nesting")!=std::string::npos) nesting=true;
    QVERIFY(nesting); QVERIFY(read(folder.filePath("output/CONVERSION.txt")).contains("nesting"));
}
void InterchangeTests::markdown_rejection_keeps_existing_data_and_files()
{
    QTemporaryDir folder; QDir root(folder.path()); QVERIFY(root.mkpath("input/attachments"));
    put(folder.filePath("input/attachments/file"),"bytes"); put(folder.filePath("input/a.md"),"# A\n\n[file](attachments/file)\n\n[bad](attachments/../outside)\n");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); const auto existing=w.create_page(db.id,"Keep");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_markdown(path(folder.filePath("input")),db.id));
    QCOMPARE(w.pages(db.id).size(),std::size_t(1)); QVERIFY(w.attachments().empty()); QVERIFY(w.audit_attachments().empty());
    QCOMPARE(w.load_page(existing.document.id).revision,existing.revision);
    put(folder.filePath("input/a.md"),QByteArray(1,char(0xff)));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_markdown(path(folder.filePath("input")),db.id));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.export_markdown(path(folder.filePath("workspace"))));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.export_markdown(path(folder.filePath("workspace/attachments/export"))));
    w.trash_database(db.id,true); put(folder.filePath("input/a.md"),"# valid\n"); QVERIFY_THROWS_EXCEPTION(std::invalid_argument,w.import_markdown(path(folder.filePath("input")),db.id));
}
void InterchangeTests::markdown_forward_missing_and_duplicate_anchors()
{
    QTemporaryDir folder; QDir root(folder.path()); QVERIFY(root.mkdir("input"));
    put(folder.filePath("input/a.md"),"# A\n\n[forward](b.md#block-target) [missing](absent.md) [missing block](b.md#block-gone)\n");
    put(folder.filePath("input/b.md"),"# B\n\n<a id=\"block-target\"></a>\n## Target\n");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    const auto report=w.import_markdown(path(folder.filePath("input")),db.id); const auto a=w.load_page(report.pages[0]), b=w.load_page(report.pages[1]);
    const auto links=w.outgoing_links(a.document.id); QCOMPARE(links.size(),std::size_t(3)); QCOMPARE(links[0].target_id,b.document.blocks[0].id);
    QCOMPARE(links[1].unresolved_name,std::string("absent.md")); QCOMPARE(links[2].kind,ReferenceKind::Block);
    QCOMPARE(w.pages(db.id).size(),std::size_t(2)); put(folder.filePath("input/b.md"),"# B\n\n<a id=\"block-target\"></a>\nOne\n\n<a id=\"block-target\"></a>\nTwo\n");
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_markdown(path(folder.filePath("input")),db.id)); QCOMPARE(w.pages(db.id).size(),std::size_t(2));
}
void InterchangeTests::markdown_sql_failure_and_missing_export_file_leave_no_partial_data()
{
    QTemporaryDir folder; QDir root(folder.path()); QVERIFY(root.mkpath("input/attachments"));
    put(folder.filePath("input/attachments/file"),"bytes"); put(folder.filePath("input/a.md"),"# New\n\n[file](attachments/file)\n");
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Keep");
    sqlite3* raw=nullptr; QCOMPARE(sqlite3_open(folder.filePath("workspace/nebula.sqlite3").toUtf8().constData(),&raw),SQLITE_OK);
    std::unique_ptr<sqlite3,DatabaseCloser> connection(raw);
    QCOMPARE(sqlite3_exec(raw,"CREATE TRIGGER fail_import BEFORE INSERT ON pages BEGIN SELECT RAISE(ABORT,'injected failure'); END",nullptr,nullptr,nullptr),SQLITE_OK);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.import_markdown(path(folder.filePath("input")),db.id));
    QCOMPARE(w.pages(db.id).size(),std::size_t(1)); QVERIFY(w.attachments().empty()); QVERIFY(w.audit_attachments().empty());
    QCOMPARE(sqlite3_exec(raw,"DROP TRIGGER fail_import",nullptr,nullptr,nullptr),SQLITE_OK);
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(folder.filePath("input/attachments/file")),false);
    QVERIFY(QFile::remove(QString::fromStdString(w.attachment_path(imported.file.id).u8string())));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,w.export_markdown(path(folder.filePath("failed-export"))));
    QVERIFY(!QFile::exists(folder.filePath("failed-export"))); QCOMPARE(w.load_page(page.document.id).revision,page.revision);
}
void InterchangeTests::markdown_image_inline_asset_and_unsafe_identity_roundtrip()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"Original",{},{{u8"見出し /#?",{},BlockKind::Heading,{{"Target"}}}});
    put(folder.filePath("image.png"),QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="));
    const auto image=w.import_attachment(page,page.document.blocks[0].id,path(folder.filePath("image.png")),true);
    page.document.blocks.back().runs={{"inline "},{"",true,false,{},Asset{true,"preview",image.file.id}},
        {" target",false,false,Reference{ReferenceKind::Block,page.document.blocks[0].id}}}; w.save_page(page);
    const auto exported=w.export_markdown(path(folder.filePath("markdown"))); QVERIFY(!exported.warnings.empty());
    const auto report=w.import_markdown(path(folder.filePath("markdown")),db.id); const auto copy=w.load_page(report.pages[0]);
    QCOMPARE(copy.document.blocks[1].kind,BlockKind::Image); QVERIFY(copy.document.blocks.back().runs[1].asset);
    QCOMPARE(copy.document.blocks.back().runs[1].asset->attachment_id,copy.document.blocks[1].attachment_id);
    QCOMPARE(copy.document.blocks.back().runs.back().reference->target_id,copy.document.blocks[0].id);
    QCOMPARE(read(QString::fromStdString(w.attachment_path(copy.document.blocks[1].attachment_id).u8string())),read(folder.filePath("image.png")));
}
QTEST_GUILESS_MAIN(InterchangeTests)
#include "interchange_tests.moc"
