#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/format/codec.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <sqlite3.h>
#include <algorithm>
#include <functional>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& value) {return std::filesystem::u8path(value.toUtf8().constData());}
void show(WorkspaceWindow& window) {
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window)); window.page_editor()->setFocus(); QApplication::processEvents();
}
void drive(WorkspaceWindow& window,const char* action,const std::function<void(QDialog*)>& callback) {
    QString unexpected;
    QTimer watchdog;
    QObject::connect(&watchdog,&QTimer::timeout,&window,[&]{
        if(auto* message=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            unexpected=message->text(); message->reject();
        }
    });
    watchdog.start(20);
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QTimer::singleShot(2000,dialog,&QDialog::reject); callback(dialog);
    });
    auto* command=window.findChild<QAction*>(action); QVERIFY(command);
    const auto button_name=QString::fromLatin1(action)+"Button";
    if(auto* button=window.findChild<QToolButton*>(button_name)) QTest::mouseClick(button,Qt::LeftButton);
    else command->trigger();
    QVERIFY2(unexpected.isEmpty(),qPrintable(unexpected));
}
void accept(QDialog* dialog) {
    if(auto* check=dialog->findChild<QCheckBox*>("confirmDatabaseTrash")) check->setChecked(true);
    auto* button=dialog->findChild<QPushButton*>("confirmTrash"); QVERIFY(button); button->click();
}
void restore(WorkspaceWindow& window,const std::string& id) {
    drive(window,"restoreTrash",[&](QDialog* dialog){
        auto* list=dialog->findChild<QListWidget*>("trashItems"); QVERIFY(list);
        QListWidgetItem* selected=nullptr;
        for(int row=0;row<list->count();++row) if(list->item(row)->data(Qt::UserRole).toString().toStdString()==id) selected=list->item(row);
        QVERIFY(selected); list->setCurrentItem(selected); dialog->findChild<QPushButton*>("restoreSelected")->click(); dialog->accept();
    });
}
void sql(const QString& file,const char* text) {
    sqlite3* raw=nullptr; const auto result=sqlite3_open(file.toUtf8().constData(),&raw);
    std::unique_ptr<sqlite3,DatabaseCloser> connection(raw);
    if(result!=SQLITE_OK || sqlite3_exec(raw,text,nullptr,nullptr,nullptr)!=SQLITE_OK) throw std::runtime_error("Test database setup failed");
}
}
class TrashSafetyTests : public QObject {
    Q_OBJECT
private slots:
    void reference_wording_and_separate_red_buttons();
    void database_requires_acknowledgement_and_restores_original_page_states();
    void page_cancel_failed_save_and_restore_keep_attachments();
    void block_subtree_is_recoverable_through_undo_redo_and_restore();
    void trashing_the_last_subtree_keeps_a_valid_page();
    void failed_block_save_restores_the_visible_content();
    void all_three_scopes_restore_after_reopening_the_workspace();
    void unavailable_scopes_are_disabled();
};
void TrashSafetyTests::reference_wording_and_separate_red_buttons()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Page");
    WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page.document.id));
    QCOMPARE(window.findChild<QLabel*>("referencedByHeading")->text(),QString("Referenced by:"));
    QCOMPARE(window.findChild<QLabel*>("referencesHeading")->text(),QString("References"));
    QCOMPARE(window.findChild<QListWidget*>("backlinks")->accessibleName(),QString("Referenced by:"));
    QCOMPARE(window.findChild<QListWidget*>("outgoingLinks")->accessibleName(),QString("References"));
    for(const auto* name:{"trashBlockButton","trashPageButton","trashDatabaseButton"}) {
        auto* button=window.findChild<QToolButton*>(name); QVERIFY(button); QVERIFY(button->isEnabled());
        QCOMPARE(button->toolButtonStyle(),Qt::ToolButtonIconOnly); QVERIFY(!button->accessibleName().isEmpty());
        const auto image=button->icon().pixmap(24,24).toImage(); QVERIFY(!image.isNull()); int red=0;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const auto color=image.pixelColor(x,y); if(color.alpha()>100 && color.red()>150 && color.green()<100 && color.blue()<100) ++red;
        }
        QVERIFY(red>15);
    }
    QCOMPARE(window.findChild<QToolButton*>("trashPageButton")->parentWidget(),window.findChild<QLineEdit*>("pageTitle")->parentWidget());
    QCOMPARE(window.findChild<QToolButton*>("trashDatabaseButton")->parentWidget(),window.findChild<QComboBox*>("databases")->parentWidget());
    QCOMPARE(window.findChild<QToolButton*>("trashBlockButton")->parentWidget(),window.findChild<QToolBar*>("blockControls"));
}
void TrashSafetyTests::database_requires_acknowledgement_and_restores_original_page_states()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Same"), other=w.create_database("Same");
    const auto first=w.create_page(db.id,"Active"), second=w.create_page(db.id,"Already trashed"), unaffected=w.create_page(other.id,"Other");
    w.trash_page(second.document.id,true); const auto old_second=w.load_page(second.document.id);
    WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(first.document.id));
    drive(window,"trashDatabase",[&](QDialog* dialog){
        QCOMPARE(dialog->objectName(),QString("confirmTrashDatabase"));
        auto* check=dialog->findChild<QCheckBox*>("confirmDatabaseTrash"); QVERIFY(check); QVERIFY(!check->isChecked());
        QVERIFY(!dialog->findChild<QPushButton*>("confirmTrash")->isEnabled());
        QVERIFY(dialog->findChild<QPushButton*>("cancelTrash")->isDefault());
        QVERIFY(dialog->findChild<QLabel*>("trashScope")->text().contains("2 pages"));
        dialog->accept(); // Even bypassing the disabled button cannot omit acknowledgement.
    });
    QCOMPARE(w.databases().size(),std::size_t(2)); QCOMPARE(w.load_page(first.document.id).revision,first.revision);
    drive(window,"trashDatabase",[](QDialog* dialog){
        dialog->findChild<QCheckBox*>("confirmDatabaseTrash")->setChecked(true); QTest::keyClick(dialog,Qt::Key_Escape);
    });
    QCOMPARE(w.databases().size(),std::size_t(2));
    drive(window,"trashDatabase",accept); QCOMPARE(w.databases().size(),std::size_t(1));
    QCOMPARE(w.resolve_reference({ReferenceKind::Page,first.document.id}).state,ReferenceState::DatabaseTrashed);
    QVERIFY(!w.load_page(first.document.id).trashed); QVERIFY(w.load_page(second.document.id).trashed);
    QCOMPARE(w.load_page(unaffected.document.id).revision,unaffected.revision);
    restore(window,db.id); QCOMPARE(w.databases().size(),std::size_t(2));
    QVERIFY(!w.load_page(first.document.id).trashed); QVERIFY(w.load_page(second.document.id).trashed);
    QCOMPARE(w.load_page(second.document.id).revision,old_second.revision);
    QCOMPARE(w.pages(db.id).size(),std::size_t(1));
}
void TrashSafetyTests::page_cancel_failed_save_and_restore_keep_attachments()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes");
    auto page=w.create_page(db.id,"With file"); const auto other=w.create_page(db.id,"Other");
    QFile source(folder.filePath("file.txt")); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("keep file bytes"); source.close();
    const auto imported=w.import_attachment(page,page.document.blocks[0].id,path(source.fileName()),false); QVERIFY(QFile::remove(source.fileName()));
    WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page.document.id));
    const auto original=encode_document(window.page_editor()->snapshot());
    drive(window,"trashPage",[](QDialog* dialog){QVERIFY(dialog->findChild<QPushButton*>("cancelTrash")->isDefault()); QTest::keyClick(dialog,Qt::Key_Return);});
    QVERIFY(!w.load_page(page.document.id).trashed); QCOMPARE(encode_document(window.page_editor()->snapshot()),original);
    QVERIFY(window.page_editor()->go_to_block(QString::fromStdString(page.document.blocks[0].id))); QTest::keyClicks(window.page_editor(),"pending");
    sql(folder.filePath("workspace/nebula.sqlite3"),"DROP TABLE page_search");
    drive(window,"trashPage",accept); QVERIFY(!w.load_page(page.document.id).trashed);
    QVERIFY(window.findChild<QLabel*>("saveState")->text().startsWith("Save failed")); QVERIFY(window.page_editor()->toPlainText().contains("pending"));
    drive(window,"trashDatabase",accept); QCOMPARE(w.databases().size(),std::size_t(1));
    QVERIFY(!w.load_page(page.document.id).trashed); QVERIFY(window.page_editor()->toPlainText().contains("pending"));
    w.rebuild_search(); QVERIFY(window.flush()); drive(window,"trashPage",accept);
    QVERIFY(w.load_page(page.document.id).trashed); QCOMPARE(w.pages(db.id).size(),std::size_t(1)); QCOMPARE(w.pages(db.id)[0].document.id,other.document.id);
    const auto file=w.attachment_path(imported.file.id); QVERIFY(std::filesystem::exists(file)); QVERIFY(w.attachment(imported.file.id));
    restore(window,page.document.id); QVERIFY(window.open_page(page.document.id)); QVERIFY(!w.load_page(page.document.id).trashed);
    QCOMPARE(w.load_page(page.document.id).document.blocks[1].attachment_id,imported.file.id);
    QFile bytes(QString::fromStdString(file.u8string())); QVERIFY(bytes.open(QIODevice::ReadOnly)); QCOMPARE(bytes.readAll(),QByteArray("keep file bytes"));
}
void TrashSafetyTests::block_subtree_is_recoverable_through_undo_redo_and_restore()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto page=w.create_page(db.id,"Tree",{},{{"root",{},BlockKind::Heading,{{"Parent"}}},
        {"child","root",BlockKind::Bullet,{{"Child"}}},{"grand","child",BlockKind::Todo,{{"Grandchild"}},{},true},
        {"sibling",{},BlockKind::Paragraph,{{"Keep"}}}});
    const auto source=w.create_page(db.id,"Source",{},{{"source",{},BlockKind::Paragraph,{{"precise",false,false,Reference{ReferenceKind::Block,"grand"}}}}});
    Q_UNUSED(source); WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page.document.id));
    const auto before=encode_document(window.page_editor()->snapshot());
    drive(window,"trashBlock",[](QDialog* dialog){QVERIFY(dialog->findChild<QLabel*>("trashScope")->text().contains("3 blocks")); dialog->reject();});
    QCOMPARE(encode_document(window.page_editor()->snapshot()),before); QCOMPARE(w.load_page(page.document.id).revision,page.revision);
    drive(window,"trashBlock",accept); auto saved=w.load_page(page.document.id);
    QCOMPARE(saved.document.blocks.size(),std::size_t(1)); QCOMPARE(saved.document.blocks[0].id,std::string("sibling"));
    QCOMPARE(w.deleted_blocks().size(),std::size_t(1)); QCOMPARE(w.resolve_reference({ReferenceKind::Block,"grand"}).state,ReferenceState::Unavailable);
    window.page_editor()->undo(); QVERIFY(window.flush()); QCOMPARE(encode_document(window.page_editor()->snapshot()),before);
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,"grand"}).state,ReferenceState::Available);
    window.page_editor()->redo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks.size(),std::size_t(1));
    const auto group=w.deleted_blocks()[0].id; restore(window,group); QVERIFY(window.open_page(page.document.id));
    QCOMPARE(encode_document(window.page_editor()->snapshot()),before); QCOMPARE(w.backlinks(ReferenceKind::Block,"grand").size(),std::size_t(1));
}
void TrashSafetyTests::trashing_the_last_subtree_keeps_a_valid_page()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto page=w.create_page(w.create_database("Notes").id,"Only",
        {},{{"only",{},BlockKind::Heading,{{"Only block"}}},{"child","only",BlockKind::Paragraph,{{"Nested"}}}});
    WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page.document.id)); drive(window,"trashBlock",accept);
    const auto saved=w.load_page(page.document.id); validate(saved.document);
    QCOMPARE(saved.document.id,page.document.id); QCOMPARE(saved.document.blocks.size(),std::size_t(1));
    QVERIFY(saved.document.blocks[0].id!="only" && saved.document.blocks[0].id!="child");
    QCOMPARE(saved.document.blocks[0].kind,BlockKind::Paragraph); QVERIFY(saved.document.blocks[0].runs.empty());
    window.page_editor()->undo(); QVERIFY(window.flush()); QCOMPARE(encode_document(w.load_page(page.document.id).document),encode_document(page.document));
}
void TrashSafetyTests::failed_block_save_restores_the_visible_content()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto page=w.create_page(w.create_database("Notes").id,"Keep",
        {},{{"first",{},BlockKind::Paragraph,{{"Keep first"}}},{"second",{},BlockKind::Paragraph,{{"Keep second"}}}});
    WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page.document.id)); const auto before=encode_document(window.page_editor()->snapshot());
    sql(folder.filePath("nebula.sqlite3"),"DROP TABLE page_search"); drive(window,"trashBlock",accept);
    QCOMPARE(encode_document(window.page_editor()->snapshot()),before); QCOMPARE(w.load_page(page.document.id).revision,page.revision);
    QVERIFY(w.deleted_blocks().empty()); QVERIFY(window.findChild<QLabel*>("saveState")->text().startsWith("Save failed"));
    w.rebuild_search(); QVERIFY(window.flush()); QCOMPARE(encode_document(w.load_page(page.document.id).document),encode_document(page.document));
}
void TrashSafetyTests::unavailable_scopes_are_disabled()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show(window);
    QVERIFY(!window.findChild<QAction*>("trashBlock")->isEnabled()); QVERIFY(!window.findChild<QAction*>("trashPage")->isEnabled());
    QVERIFY(window.findChild<QAction*>("trashDatabase")->isEnabled()); drive(window,"trashDatabase",accept);
    QVERIFY(!window.findChild<QAction*>("trashDatabase")->isEnabled()); QVERIFY(window.findChild<QAction*>("restoreTrash")->isEnabled());
}
void TrashSafetyTests::all_three_scopes_restore_after_reopening_the_workspace()
{
    QTemporaryDir folder;
    QFile source(folder.filePath("kept.txt")); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("retained after restart"); source.close();
    std::string database,page_id,group,file_id,card;
    {
        auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Restart"); database=db.id;
        auto page=w.create_page(database,"Recover",{},{{"keep",{},BlockKind::Paragraph,{{"Keep"}}}}); page_id=page.document.id;
        const auto imported=w.import_attachment(page,"keep",path(source.fileName()),false); file_id=imported.file.id; card=imported.card;
        QVERIFY(QFile::remove(source.fileName()));
        WorkspaceWindow window(w); show(window); QVERIFY(window.open_page(page_id));
        QVERIFY(window.page_editor()->go_to_block(QString::fromStdString(card)));
        drive(window,"trashBlock",accept); QCOMPARE(w.deleted_blocks().size(),std::size_t(1)); group=w.deleted_blocks()[0].id;
        drive(window,"trashPage",accept); QVERIFY(w.load_page(page_id).trashed);
        drive(window,"trashDatabase",accept); QCOMPARE(w.databases().size(),std::size_t(0));
        QVERIFY(window.close());
    }
    {
        auto w=Workspace::open(path(folder.filePath("workspace"))); WorkspaceWindow window(w); show(window);
        QVERIFY(w.load_page(page_id).trashed); QCOMPARE(w.deleted_blocks()[0].id,group);
        restore(window,database); QVERIFY(w.load_page(page_id).trashed);
        restore(window,page_id); restore(window,group); QVERIFY(window.open_page(page_id));
        const auto saved=w.load_page(page_id); QCOMPARE(saved.database_id,database);
        const auto block=std::find_if(saved.document.blocks.begin(),saved.document.blocks.end(),[&](const auto& value){return value.id==card;});
        QVERIFY(block!=saved.document.blocks.end()); QCOMPARE(block->attachment_id,file_id);
        QFile bytes(QString::fromStdString(w.attachment_path(file_id).u8string()));
        QVERIFY(bytes.open(QIODevice::ReadOnly)); QCOMPARE(bytes.readAll(),QByteArray("retained after restart"));
        QVERIFY(w.deleted_blocks().empty());
    }
}
QTEST_MAIN(TrashSafetyTests)
#include "trash_safety_tests.moc"
