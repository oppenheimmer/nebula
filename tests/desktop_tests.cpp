#include "nebula/app/prototype_window.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/storage/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QTimer>
#include <sqlite3.h>
#include <algorithm>

using namespace nebula;

namespace {
std::filesystem::path path(const QString& value) { return std::filesystem::u8path(value.toUtf8().constData()); }

QByteArray contents(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void sql(const QString& filename, const char* command)
{
    sqlite3* raw = nullptr;
    const int result = sqlite3_open(filename.toUtf8().constData(), &raw);
    std::unique_ptr<sqlite3, DatabaseCloser> database(raw);
    if (result != SQLITE_OK || sqlite3_exec(raw, command, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Test database setup failed");
}

void show_window(PrototypeWindow& window)
{
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    window.page_editor()->setFocus();
    QApplication::processEvents();
}

void click_action(PrototypeWindow& window, const char* name)
{
    auto* action = window.findChild<QAction*>(name);
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    QWidget* button = nullptr;
    for (auto* toolbar : window.findChildren<QToolBar*>()) {
        if (auto* candidate = toolbar->widgetForAction(action))
            button = candidate;
    }
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();
}
} // namespace

class DesktopTests : public QObject {
    Q_OBJECT
private slots:
    void default_location_is_suggested_without_creating_it();
    void cancel_does_not_create_a_workspace();
    void accepting_expands_home_and_normalizes_path();
    void invalid_paths_keep_the_dialog_open();
    void browse_selects_a_folder_and_cancel_preserves_it();
    void create_and_reopen_workspace_preserves_identity();
    void unrelated_database_is_not_modified();
    void unsupported_workspace_is_not_modified();
    void invalid_attachment_location_is_rejected();
    void read_only_workspace_reports_failure();
    void command_line_workspace_survives_process_exit();
    void default_window_has_one_block_editor();
    void toolbar_moves_nests_and_restores_focus();
    void real_window_keyboard_changes_blocks_and_counter();
    void keypad_modifier_does_not_disable_block_shortcuts();
    void unavailable_block_actions_are_disabled();
    void comparison_requires_an_explicit_option();
    void active_outline_tracks_wrapping_and_scrolling();
    void persistent_window_autosaves_and_reopens_real_notes();
    void switching_pages_flushes_pending_edits();
    void table_edits_preserve_typed_properties_and_identity();
    void saved_view_renders_numeric_order_and_filters();
    void failed_autosave_keeps_edits_visible();
    void persistent_links_navigate_and_show_backlinks();
    void view_dialog_saves_filter_and_column_settings();
    void database_and_trash_actions_preserve_records();
    void journal_entry_preserves_the_selected_calendar_date();
};

void DesktopTests::default_location_is_suggested_without_creating_it()
{
    QTemporaryDir home;
    WorkspaceDialog dialog(home.path());
    QCOMPARE(dialog.directory(), QDir(home.path()).filePath("Document/nebula"));
    QVERIFY(!QFileInfo::exists(dialog.directory()));
}

void DesktopTests::cancel_does_not_create_a_workspace()
{
    QTemporaryDir home;
    WorkspaceDialog dialog(home.path()); dialog.show();
    QTest::mouseClick(dialog.findChild<QPushButton*>("cancelWorkspace"), Qt::LeftButton);
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(!QFileInfo::exists(QDir(home.path()).filePath("Document")));
}

void DesktopTests::accepting_expands_home_and_normalizes_path()
{
    QTemporaryDir home;
    WorkspaceDialog dialog(home.path()); dialog.show();
    dialog.set_directory(QString::fromUtf8(u8"~/notes/../Diary 日本語"));
    QTest::mouseClick(dialog.findChild<QPushButton*>("openWorkspace"), Qt::LeftButton);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.directory(), QDir(home.path()).filePath(QString::fromUtf8(u8"Diary 日本語")));
    QVERIFY(!QFileInfo::exists(dialog.directory())); // Selection alone has no filesystem side effects.
}

void DesktopTests::invalid_paths_keep_the_dialog_open()
{
    QTemporaryDir home;
    QFile file(home.filePath("file.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("notes"); file.close();
    WorkspaceDialog dialog(home.path()); dialog.show();
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    for (const auto& value : {QString{}, QString("relative/folder"), file.fileName()}) {
        dialog.set_directory(value); dialog.accept();
        QCOMPARE(accepted.count(), 0);
        QVERIFY(!dialog.findChild<QLabel*>("workspaceError")->text().isEmpty());
    }
    QCOMPARE(contents(file.fileName()), QByteArray("notes"));
}

void DesktopTests::browse_selects_a_folder_and_cancel_preserves_it()
{
    QTemporaryDir home;
    int calls = 0;
    QString first_start;
    WorkspaceDialog dialog(home.path(), nullptr, [&](const QString& start) {
        ++calls;
        if (calls == 1) {
            first_start = start;
            return home.path();
        }
        return QString{};
    });
    dialog.show();
    QTest::mouseClick(dialog.findChild<QPushButton*>("browseWorkspace"), Qt::LeftButton);
    QCOMPARE(first_start, default_workspace_directory(home.path()));
    QCOMPARE(dialog.directory(), home.path());
    QTest::mouseClick(dialog.findChild<QPushButton*>("browseWorkspace"), Qt::LeftButton);
    QCOMPARE(dialog.directory(), home.path());
    QCOMPARE(calls, 2);
}

void DesktopTests::create_and_reopen_workspace_preserves_identity()
{
    QTemporaryDir root;
    const auto folder = root.filePath(QString::fromUtf8(u8"Diary 日本語 with spaces"));
    std::string id;
    {
        const auto workspace = Workspace::open(path(folder));
        id = workspace.id(); QCOMPARE(id.size(), std::size_t(32));
        QVERIFY(QFileInfo::exists(folder + "/attachments"));
        QVERIFY(contents(folder + "/nebula.sqlite3").startsWith("SQLite format 3"));
    }
    const auto workspace = Workspace::open(path(folder));
    QCOMPARE(workspace.id(), id);
    QCOMPARE(workspace.database_path().u8string(), path(folder + "/nebula.sqlite3").u8string());
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Workspace::open("relative"));
}

void DesktopTests::unrelated_database_is_not_modified()
{
    QTemporaryDir folder;
    const auto filename = folder.filePath("nebula.sqlite3");
    // "sqliteNotes" is a legal user name, unlike SQLite's reserved "sqlite_" prefix.
    sql(filename, "CREATE TABLE sqliteNotes(text TEXT); INSERT INTO sqliteNotes VALUES('keep me')");
    const auto before = contents(filename);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, Workspace::open(path(folder.path())));
    QCOMPARE(contents(filename), before);
    QVERIFY(!QFileInfo::exists(folder.filePath("attachments")));
    QFile invalid(filename); QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalid.write("not a database"); invalid.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, Workspace::open(path(folder.path())));
    QCOMPARE(contents(filename), QByteArray("not a database"));
}

void DesktopTests::unsupported_workspace_is_not_modified()
{
    QTemporaryDir folder;
    { const auto workspace = Workspace::open(path(folder.path())); }
    const auto filename = folder.filePath("nebula.sqlite3");
    sql(filename, "PRAGMA user_version=999");
    const auto before = contents(filename);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, Workspace::open(path(folder.path())));
    QCOMPARE(contents(filename), before);
}

void DesktopTests::invalid_attachment_location_is_rejected()
{
    QTemporaryDir folder;
    QFile file(folder.filePath("attachments")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, Workspace::open(path(folder.path())));
    QCOMPARE(contents(file.fileName()), QByteArray("keep"));
    QVERIFY(!QFileInfo::exists(folder.filePath("nebula.sqlite3")));
}

void DesktopTests::read_only_workspace_reports_failure()
{
    QTemporaryDir folder;
    { const auto workspace = Workspace::open(path(folder.path())); }
    const auto filename = folder.filePath("nebula.sqlite3");
    const auto before = contents(filename);
    const auto permissions = QFile::permissions(filename);
    QVERIFY(QFile::setPermissions(filename, QFileDevice::ReadOwner));
    bool failed = false;
    try { auto workspace = Workspace::open(path(folder.path())); }
    catch (const std::runtime_error&) { failed = true; }
    QVERIFY(QFile::setPermissions(filename, permissions));
    QVERIFY(failed);
    QCOMPARE(contents(filename), before);
}

void DesktopTests::command_line_workspace_survives_process_exit()
{
    QTemporaryDir root;
    const auto folder = root.filePath("chosen workspace");
    for (int i = 0; i < 2; ++i) {
        QProcess process;
        process.start(QCoreApplication::applicationDirPath() + "/nebula_prototype",
                      {"--workspace", folder, "--smoke-test"});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(QFileInfo::exists(folder + "/nebula.sqlite3"));
    }
    const auto workspace = Workspace::open(path(folder));
    QVERIFY(!workspace.id().empty());
}

void DesktopTests::default_window_has_one_block_editor()
{
    PrototypeWindow window; show_window(window);
    QCOMPARE(window.findChildren<QTextEdit*>().size(), 1);
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(window.page_editor()));
    QVERIFY(!window.findChild<QTabWidget*>());
    QVERIFY(window.page_editor()->active_block_rect().contains(QRectF(window.page_editor()->cursorRect())));
}

void DesktopTests::toolbar_moves_nests_and_restores_focus()
{
    PrototypeWindow window; show_window(window);
    auto* editor = window.page_editor(); editor->go_to_block("intro"); QApplication::processEvents();
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, editor->cursorRect().center());
    click_action(window, "moveDown");
    QCOMPARE(editor->snapshot().blocks[3].id, std::string("intro"));
    click_action(window, "indent");
    QCOMPARE(editor->snapshot().blocks[3].parent_id, std::string("list"));
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget*>(editor));
    click_action(window, "outdent");
    QVERIFY(editor->snapshot().blocks[3].parent_id.empty());
    QVERIFY(window.findChild<QLabel*>("activeBlock")->text().contains("Nesting level 0"));
}

void DesktopTests::real_window_keyboard_changes_blocks_and_counter()
{
    PrototypeWindow window; show_window(window);
    auto* editor = window.page_editor(); editor->go_to_block("intro"); QApplication::processEvents();
    QVERIFY(QApplication::focusWidget());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(editor->snapshot().blocks[3].id, std::string("intro"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QCOMPARE(editor->snapshot().blocks[3].parent_id, std::string("list"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(editor->snapshot().blocks[3].parent_id.empty());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QCOMPARE(editor->snapshot().blocks.size(), std::size_t(10));
    QApplication::processEvents();
    QVERIFY(window.findChild<QLabel*>("activeBlock")->text().contains("of 10"));
}

void DesktopTests::keypad_modifier_does_not_disable_block_shortcuts()
{
    PrototypeWindow window; show_window(window);
    auto* editor = window.page_editor(); editor->go_to_block("intro"); QApplication::processEvents();
    QVERIFY(QApplication::focusWidget());
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down, Qt::AltModifier | Qt::KeypadModifier);
    QCOMPARE(editor->snapshot().blocks[3].id, std::string("intro"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab, Qt::KeypadModifier);
    QCOMPARE(editor->snapshot().blocks[3].parent_id, std::string("list"));
}

void DesktopTests::unavailable_block_actions_are_disabled()
{
    PrototypeWindow window; show_window(window);
    QVERIFY(!window.findChild<QAction*>("moveUp")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("indent")->isEnabled());
    QVERIFY(!window.findChild<QAction*>("outdent")->isEnabled());
    QVERIFY(window.findChild<QAction*>("moveDown")->isEnabled());
}

void DesktopTests::comparison_requires_an_explicit_option()
{
    PrototypeWindow window(sample_page(), {}, true); show_window(window);
    auto* tabs = window.findChild<QTabWidget*>(); QVERIFY(tabs); QCOMPARE(tabs->count(), 2);
    tabs->setCurrentIndex(1); QApplication::processEvents();
    QVERIFY(!window.findChild<QToolBar*>("blockControls")->isEnabled());
    QVERIFY(window.findChild<QLabel*>("activeBlock")->text().startsWith("Comparison mode"));
}

void DesktopTests::active_outline_tracks_wrapping_and_scrolling()
{
    Page page{"outline", {{"wrapped", "", BlockKind::Paragraph,
        {{std::string(500, 'w')}}}, {"second", "", BlockKind::Paragraph, {{"Next block"}}}}};
    PrototypeWindow window(page); show_window(window);
    auto* editor = window.page_editor();
    const auto wrapped = editor->active_block_rect();
    QVERIFY(wrapped.height() > editor->fontMetrics().height() * 3);
    const auto with_outline = editor->viewport()->grab().toImage();
    QVERIFY(editor->go_to_block("second"));
    const auto second = editor->active_block_rect();
    QVERIFY(second.top() > wrapped.top());
    QVERIFY(second.height() < wrapped.height());
    const auto without_outline = editor->viewport()->grab().toImage();
    // Pixels at the old outline's top edge change, outside the document text.
    const auto edge = QPoint(20, qRound(wrapped.top()));
    QVERIFY(with_outline.pixelColor(edge) != without_outline.pixelColor(edge));
    editor->set_page(long_page(100));
    QVERIFY(editor->go_to_block("block-80"));
    QApplication::processEvents();
    QVERIFY(editor->verticalScrollBar()->value() > 0);
    QVERIFY(editor->active_block_rect().intersects(QRectF(editor->viewport()->rect())));
}

void DesktopTests::persistent_window_autosaves_and_reopens_real_notes()
{
    QTemporaryDir folder; std::string id;
    {
        auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show_window(window);
        window.create_page("Diary"); auto* table=window.findChild<QTableWidget*>("databaseTable");
        QCOMPARE(table->rowCount(),1); id=table->item(0,0)->data(Qt::UserRole).toString().toStdString();
        QTest::keyClicks(window.page_editor(),"Remember this"); QTest::keyClick(window.page_editor(),Qt::Key_Return);
        QTest::keyClicks(window.page_editor(),"Second block");
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QLabel*>("saveState")->text(),QString("Saved locally"),3000);
        QCOMPARE(w.load_page(id).document.blocks.size(),std::size_t(2));
        auto* title=window.findChild<QLineEdit*>("pageTitle"); title->setFocus(); title->selectAll(); QTest::keyClicks(title,"Renamed diary");
        QVERIFY(window.close()); // Close flushes before the debounce interval.
    }
    auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show_window(window);
    auto* table=window.findChild<QTableWidget*>("databaseTable"); QCOMPARE(table->rowCount(),1);
    QCOMPARE(table->item(0,0)->text(),QString("Renamed diary"));
    QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualItemRect(table->item(0,0)).center());
    QCOMPARE(window.page_editor()->snapshot().id,id);
    QCOMPARE(window.page_editor()->snapshot().blocks[0].runs[0].text,std::string("Remember this"));
    QCOMPARE(window.page_editor()->snapshot().blocks[1].runs[0].text,std::string("Second block"));
}
void DesktopTests::switching_pages_flushes_pending_edits()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show_window(window);
    window.create_page("First"); const auto first=window.page_editor()->snapshot().id;
    QTest::keyClicks(window.page_editor(),"Pending first");
    window.create_page("Second"); const auto second=window.page_editor()->snapshot().id;
    QTest::keyClicks(window.page_editor(),"Pending second"); QVERIFY(window.open_page(first));
    QCOMPARE(w.load_page(second).document.blocks[0].runs[0].text,std::string("Pending second"));
    QCOMPARE(window.page_editor()->snapshot().blocks[0].runs[0].text,std::string("Pending first"));
    QTest::keyClick(window.page_editor(),Qt::Key_Return); QVERIFY(window.flush());
    QTest::keyClick(window.page_editor(),Qt::Key_Z,Qt::ControlModifier); QVERIFY(window.flush());
    QCOMPARE(w.load_page(first).document.blocks.size(),std::size_t(1)); // Save did not clear undo.
}
void DesktopTests::table_edits_preserve_typed_properties_and_identity()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Numbers");
    const auto def=w.add_property(db.id,"Score",PropertyKind::Number); const auto page=w.create_page(db.id,"Page");
    WorkspaceWindow window(w); show_window(window); auto* table=window.findChild<QTableWidget*>("databaseTable");
    QCOMPARE(table->columnCount(),2); table->item(0,1)->setText("12.5");
    QCOMPARE(std::get<double>(w.load_page(page.document.id).properties.at(def.id)),12.5);
    table->item(0,0)->setText("Renamed"); QCOMPARE(w.load_page(page.document.id).title,std::string("Renamed"));
    QCOMPARE(window.page_editor()->snapshot().id,page.document.id);
}
void DesktopTests::saved_view_renders_numeric_order_and_filters()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Scores"); auto def=w.add_property(db.id,"Score",PropertyKind::Number);
    auto a=w.create_page(db.id,"Ten"); a.properties[def.id]=10.0; w.save_page(a);
    auto b=w.create_page(db.id,"Two"); b.properties[def.id]=2.0; w.save_page(b);
    View all; all.database_id=db.id; all.name="All"; all.visible_properties={def.id}; all.sort_property=def.id; w.save_view(all);
    auto filtered=all; filtered.id.clear(); filtered.name="Large"; filtered.filters={{def.id,Comparison::Greater,5.0}}; w.save_view(filtered);
    WorkspaceWindow window(w); show_window(window); auto* table=window.findChild<QTableWidget*>("databaseTable");
    QCOMPARE(table->rowCount(),2); QCOMPARE(table->item(0,0)->text(),QString("Two"));
    window.findChild<QComboBox*>("savedViews")->setCurrentIndex(1); QCOMPARE(table->rowCount(),1); QCOMPARE(table->item(0,0)->text(),QString("Ten"));
}
void DesktopTests::failed_autosave_keeps_edits_visible()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Notes"); auto p=w.create_page(db.id,"Page");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(p.document.id));
    p.title="External change"; w.save_page(p);
    QTest::keyClicks(window.page_editor(),"Do not lose this"); QVERIFY(!window.flush());
    QVERIFY(window.findChild<QLabel*>("saveState")->text().startsWith("Save failed"));
    QCOMPARE(window.page_editor()->toPlainText(),QString("Do not lose this"));
    QCOMPARE(w.load_page(p.document.id).title,std::string("External change"));
    QVERIFY(w.load_page(p.document.id).document.blocks[0].runs.empty());
    QTimer::singleShot(0,&window,[]{ auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Cancel)->click(); });
    QVERIFY(!window.close()); QVERIFY(window.isVisible());
    QTimer::singleShot(0,&window,[]{ auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Discard)->click(); });
    QVERIFY(window.close());
}
void DesktopTests::persistent_links_navigate_and_show_backlinks()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); auto db=w.create_database("Notes"); auto target=w.create_page(db.id,"Target"); auto source=w.create_page(db.id,"Source");
    source.document.blocks[0].runs={{"Target",false,false,Reference{ReferenceKind::Page,target.document.id}}}; w.save_page(source);
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    window.page_editor()->reference_activated(int(ReferenceKind::Page),QString::fromStdString(target.document.id));
    QCOMPARE(window.page_editor()->snapshot().id,target.document.id);
    QCOMPARE(window.findChild<QListWidget*>("backlinks")->count(),1);
    QCOMPARE(window.findChild<QListWidget*>("backlinks")->item(0)->text(),QString("Source"));
}
void DesktopTests::view_dialog_saves_filter_and_column_settings()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Scores"); const auto n=w.add_property(db.id,"Score",PropertyKind::Number);
    auto a=w.create_page(db.id,"Two"); a.properties[n.id]=2.0; w.save_page(a);
    auto b=w.create_page(db.id,"Ten"); b.properties[n.id]=10.0; w.save_page(b);
    WorkspaceWindow window(w); show_window(window);
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        dialog->findChild<QLineEdit*>("viewName")->setText("Large"); dialog->findChild<QComboBox*>("viewSort")->setCurrentIndex(1);
        dialog->findChild<QPushButton*>("addViewFilter")->click(); auto* filters=dialog->findChild<QTableWidget*>("viewFilters");
        qobject_cast<QComboBox*>(filters->cellWidget(0,0))->setCurrentIndex(1);
        qobject_cast<QComboBox*>(filters->cellWidget(0,1))->setCurrentIndex(int(Comparison::Greater));
        qobject_cast<QLineEdit*>(filters->cellWidget(0,2))->setText("5");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    window.findChild<QAction*>("newView")->trigger();
    auto* table=window.findChild<QTableWidget*>("databaseTable"); QCOMPARE(table->rowCount(),1); QCOMPARE(table->item(0,0)->text(),QString("Ten"));
    const auto views=w.views(db.id); QCOMPARE(views.size(),std::size_t(2)); QCOMPARE(views[1].name,std::string("Large"));
    QCOMPARE(views[1].filters.size(),std::size_t(1)); QCOMPARE(views[1].visible_properties[0],n.id);
    window.findChild<QComboBox*>("savedViews")->setCurrentIndex(0); QCOMPARE(table->rowCount(),2);
}
void DesktopTests::database_and_trash_actions_preserve_records()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show_window(window);
    auto enter_name=[&](const char* action,const QString& text) {
        QTimer::singleShot(0,&window,[text]{ auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->setTextValue(text); dialog->accept(); });
        window.findChild<QAction*>(action)->trigger();
    };
    enter_name("newDatabase","Notebook"); QCOMPARE(w.databases().size(),std::size_t(2));
    const auto db_id=window.findChild<QComboBox*>("databases")->currentData().toString().toStdString();
    enter_name("renameDatabase","Renamed notebook");
    const auto dbs=w.databases(); QVERIFY(std::any_of(dbs.begin(),dbs.end(),[&](const auto& db){return db.id==db_id && db.name=="Renamed notebook";}));
    window.create_page("Restore me"); const auto id=window.page_editor()->snapshot().id;
    window.findChild<QAction*>("trashPage")->trigger(); QVERIFY(w.load_page(id).trashed);
    auto restore_first=[&]{
        QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); auto* list=dialog->findChild<QListWidget*>("trashItems"); QVERIFY(list->count()>0); list->setCurrentRow(0); dialog->findChild<QPushButton*>("restoreSelected")->click(); dialog->accept(); });
        window.findChild<QAction*>("restoreTrash")->trigger();
    };
    restore_first(); QVERIFY(!w.load_page(id).trashed); QCOMPARE(w.pages(db_id).size(),std::size_t(1));
    window.findChild<QAction*>("trashDatabase")->trigger(); QCOMPARE(w.databases().size(),std::size_t(1));
    restore_first(); QCOMPARE(w.databases().size(),std::size_t(2)); QCOMPARE(w.load_page(id).document.id,id);
}
void DesktopTests::journal_entry_preserves_the_selected_calendar_date()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); WorkspaceWindow window(w); show_window(window);
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->setTextValue("2024-03-10"); dialog->accept(); });
    window.findChild<QAction*>("newJournalEntry")->trigger();
    const auto id=window.page_editor()->snapshot().id;
    auto page=w.load_page(id); QCOMPARE(page.title,std::string("2024-03-10"));
    const auto defs=w.properties(page.database_id); QCOMPARE(defs.size(),std::size_t(3));
    const auto date=std::find_if(defs.begin(),defs.end(),[](const auto& d){return d.name=="Entry date";}); QVERIFY(date!=defs.end());
    QCOMPARE(std::get<Date>(page.properties.at(date->id)).iso,std::string("2024-03-10"));
    auto reopened=Workspace::open(path(folder.path()));
    QCOMPARE(std::get<Date>(reopened.load_page(id).properties.at(date->id)).iso,std::string("2024-03-10"));
}

QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
