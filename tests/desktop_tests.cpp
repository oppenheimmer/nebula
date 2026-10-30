#include "nebula/app/prototype_window.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/storage/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QDir>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QTimer>
#include <QTreeWidget>
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
    void mouse_reordered_subtree_persists_and_undoes();
    void unresolved_link_picker_does_not_create_or_bind_pages();
    void deleted_blocks_can_be_restored_from_the_window();
    void toolbar_divider_persists_and_undoes();
    void typing_undo_group_survives_autosave();
    void rich_clipboard_persists_remapped_links_and_undo();
    void block_transfer_saves_pending_edits_and_has_workspace_undo();
    void failed_transfer_undo_preserves_later_edits();
    void page_move_control_preserves_properties_and_focus();
    void unavailable_references_preserve_current_page_and_identity();
    void failed_save_blocks_workspace_moves();
    void typed_wiki_links_persist_page_heading_block_and_unresolved_targets();
    void wiki_completion_selects_a_specific_duplicate_target();
    void ambiguous_wiki_links_require_explicit_choice_or_preserve_text();
    void keyboard_reference_follow_and_same_page_history_restore_positions();
    void cross_database_history_preserves_selection_scroll_and_forward_branch();
    void outgoing_and_backlink_lists_navigate_exact_blocks_with_keyboard();
    void history_refuses_trash_and_failed_saves_without_losing_entries();
    void default_and_custom_labels_follow_renames_without_retargeting();
    void unresolved_creation_chooses_database_and_undo_keeps_created_page();
    void unresolved_existing_choice_replaces_only_the_selected_reference();
    void unresolved_creation_failure_keeps_reference_and_no_orphan_page();
    void managed_file_import_export_undo_and_reopen();
    void managed_image_preview_uses_copied_bytes_and_reports_missing_file();
    void import_cancel_and_invalid_image_preserve_page();
    void attachment_audit_control_removes_only_unused_files();
    void journal_with_image_and_file_restores_into_fresh_window();
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
    QVERIFY(window.findChild<QListWidget*>("backlinks")->item(0)->text().startsWith("Source — "));
    QVERIFY(window.findChild<QListWidget*>("backlinks")->item(0)->text().contains("Target"));
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
void DesktopTests::mouse_reordered_subtree_persists_and_undoes()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); auto page=w.create_page(db.id,"Drag me");
    const auto id=page.document.id; page.document=sample_page(); page.document.id=id; w.save_page(page);
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(id));
    auto* editor=window.page_editor(); editor->go_to_block("todo"); const auto target=QPoint(18,qRound(editor->active_block_rect().bottom()-2));
    editor->go_to_block("list"); QTest::mousePress(editor->viewport(),Qt::LeftButton,Qt::NoModifier,editor->grip_rect().center().toPoint());
    QTest::mouseMove(editor->viewport(),target); QTest::mouseRelease(editor->viewport(),Qt::LeftButton,Qt::NoModifier,target);
    QVERIFY(window.flush()); const auto saved=w.load_page(id); QCOMPARE(saved.document.blocks[3].id,std::string("list"));
    QCOMPARE(saved.document.blocks[4].parent_id,std::string("list"));
    editor->undo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(id).document.blocks[2].id,std::string("list"));
}
void DesktopTests::unresolved_link_picker_does_not_create_or_bind_pages()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); const auto p=w.create_page(db.id,"Source");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(p.document.id));
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->findChild<QLineEdit*>("unresolvedName")->setText("Future"); dialog->findChild<QPushButton*>("insertUnresolved")->click(); });
    window.findChild<QAction*>("insertLink")->trigger(); QVERIFY(window.flush());
    QCOMPARE(w.pages(db.id).size(),std::size_t(1)); const auto ref=w.load_page(p.document.id).document.blocks[0].runs[0].reference;
    QVERIFY(ref); QVERIFY(ref->target_id.empty()); QCOMPARE(ref->database_scope,db.id);
    const auto same=w.create_page(db.id,"Future");
    QVERIFY(w.backlinks(ReferenceKind::Page,same.document.id).empty());
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); QCOMPARE(dialog->objectName(),QString("resolveUnresolved")); dialog->reject(); });
    window.page_editor()->unresolved_reference_activated("Future",QString::fromStdString(db.id));
    QCOMPARE(window.page_editor()->snapshot().id,p.document.id); QCOMPARE(w.pages(db.id).size(),std::size_t(2));
}
void DesktopTests::deleted_blocks_can_be_restored_from_the_window()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); auto p=w.create_page(db.id,"Recover");
    p.document.blocks.push_back({"recover-me","",BlockKind::Paragraph,{{"Deleted content"}}}); w.save_page(p);
    p.document.blocks.pop_back(); w.save_page(p); QCOMPARE(w.deleted_blocks().size(),std::size_t(1));
    WorkspaceWindow window(w); show_window(window);
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); auto* list=dialog->findChild<QListWidget*>("trashItems"); QCOMPARE(list->count(),1); list->setCurrentRow(0); dialog->findChild<QPushButton*>("restoreSelected")->click(); dialog->accept(); });
    window.findChild<QAction*>("restoreTrash")->trigger(); QVERIFY(w.deleted_blocks().empty());
    QVERIFY(window.open_page(p.document.id)); QCOMPARE(window.page_editor()->snapshot().blocks[1].id,std::string("recover-me"));
}
void DesktopTests::toolbar_divider_persists_and_undoes()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Divider");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    click_action(window,"insertDivider"); QVERIFY(window.flush());
    QCOMPARE(w.load_page(page.document.id).document.blocks[1].kind,BlockKind::Divider);
    QCOMPARE(QApplication::focusWidget(),window.page_editor());
    window.page_editor()->undo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks.size(),std::size_t(1));
}
void DesktopTests::typing_undo_group_survives_autosave()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Undo typing");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    auto* editor=window.page_editor(); QTest::keyClicks(editor,"one"); QVERIFY(window.flush());
    QTest::keyClicks(editor," two"); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks[0].runs[0].text,std::string("one two"));
    editor->undo(); QVERIFY(window.flush()); QVERIFY(w.load_page(page.document.id).document.blocks[0].runs.empty());
    editor->redo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks[0].runs[0].text,std::string("one two"));
}
void DesktopTests::rich_clipboard_persists_remapped_links_and_undo()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto source=w.create_page(db.id,"Source",{},{{"heading","",BlockKind::Heading,{{"Heading",true}}},{"link","",BlockKind::Paragraph,{{"Jump",false,true,Reference{ReferenceKind::Block,"heading","","",false}}}}});
    const auto target=w.create_page(db.id,"Target"); WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    auto* editor=window.page_editor(); auto selected=editor->textCursor(); selected.select(QTextCursor::Document); editor->setTextCursor(selected); QTest::keyClick(editor,Qt::Key_C,Qt::ControlModifier);
    QVERIFY(window.open_page(target.document.id)); QTest::keyClick(editor,Qt::Key_V,Qt::ControlModifier); QVERIFY(window.flush());
    const auto copied=w.load_page(target.document.id); QCOMPARE(copied.document.blocks.size(),std::size_t(2));
    QCOMPARE(copied.document.blocks[0].kind,BlockKind::Heading); QVERIFY(copied.document.blocks[1].runs[0].italic);
    QCOMPARE(copied.document.blocks[1].runs[0].reference->target_id,copied.document.blocks[0].id);
    QVERIFY(!copied.document.blocks[1].runs[0].reference->custom_label); QCOMPARE(copied.document.blocks[1].runs[0].text,std::string("Heading"));
    QCOMPARE(w.backlinks(ReferenceKind::Block,copied.document.blocks[0].id).size(),std::size_t(1));
    editor->undo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(target.document.id).document.blocks.size(),std::size_t(1));
    editor->redo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(target.document.id).document.blocks[0].id,copied.document.blocks[0].id);
}

void DesktopTests::block_transfer_saves_pending_edits_and_has_workspace_undo()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path()));
    const auto db=w.create_database("Notes"), other_db=w.create_database("Other");
    const auto source=w.create_page(db.id,"Source",{},{{"heading","",BlockKind::Heading,{{"Heading"}}},
        {"child","heading",BlockKind::Paragraph,{{"Child"}}},{"remain","",BlockKind::Paragraph,{{"Remain"}}}});
    const auto destination=w.create_page(other_db.id,"Duplicate"); w.create_page(db.id,"Duplicate");
    const auto ref=w.create_page(db.id,"References",{},{{"ref","",BlockKind::Paragraph,{{"Jump",false,false,Reference{ReferenceKind::Block,"child"}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    auto* editor=window.page_editor(); editor->go_to_block("heading");
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject(); });
    window.findChild<QAction*>("moveBlockToPage")->trigger();
    QCOMPARE(editor->snapshot().blocks.size(),std::size_t(3));
    QVERIFY(!window.findChild<QAction*>("undoBlockTransfer")->isEnabled());
    QVERIFY(QTest::qWaitForWindowActive(&window)); QTRY_COMPARE(QApplication::focusWidget(),editor);
    QTest::keyClick(editor,Qt::Key_End); QTest::keyClicks(editor," edited");
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto* choices=dialog->findChild<QComboBox*>("moveDestinationPage"); QVERIFY(choices);
        choices->setCurrentIndex(choices->findData(QString::fromStdString(destination.document.id)));
        QVERIFY(choices->currentText().startsWith("Other / Duplicate"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    QTest::keyClick(editor,Qt::Key_M,Qt::ControlModifier|Qt::ShiftModifier);
    QCOMPARE(editor->snapshot().blocks.size(),std::size_t(1)); QCOMPARE(editor->snapshot().blocks[0].id,std::string("remain"));
    QCOMPARE(w.load_page(destination.document.id).document.blocks[1].runs[0].text,std::string("Heading edited"));
    QVERIFY(!editor->document()->isUndoAvailable()); QTRY_COMPARE(QApplication::focusWidget(),editor);
    QVERIFY(window.open_page(ref.document.id));
    editor->reference_activated(int(ReferenceKind::Block),"child");
    QCOMPARE(editor->snapshot().id,destination.document.id); QCOMPARE(editor->current_block_id(),QString("child"));
    QCOMPARE(window.findChild<QListWidget*>("backlinks")->count(),1);
    QString unexpected_error; QTimer dismiss_error;
    connect(&dismiss_error,&QTimer::timeout,&window,[&]{
        if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { unexpected_error=box->text(); box->accept(); }
    });
    dismiss_error.start(0);
    QTest::keyClick(editor,Qt::Key_Z,Qt::ControlModifier|Qt::AltModifier);
    dismiss_error.stop(); QVERIFY2(unexpected_error.isEmpty(),qPrintable(unexpected_error));
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,"child"}).page_id,source.document.id);
    QVERIFY(window.findChild<QAction*>("redoBlockTransfer")->isEnabled());
    QCOMPARE(editor->snapshot().blocks.size(),std::size_t(1));
    QTest::keyClick(editor,Qt::Key_Z,Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier);
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,"child"}).page_id,destination.document.id);
    QVERIFY(window.open_page(ref.document.id)); editor->reference_activated(int(ReferenceKind::Block),"child");
    QCOMPARE(editor->current_block_id(),QString("child")); QVERIFY(window.flush());
    auto reopened=Workspace::open(path(folder.path()));
    QCOMPARE(reopened.load_page(destination.document.id).document.blocks[2].parent_id,std::string("heading"));
    QVERIFY(window.open_page(ref.document.id)); QTest::keyClicks(editor,"Context ");
    QVERIFY(editor->document()->isUndoAvailable()); window.findChild<QAction*>("undoBlockTransfer")->trigger();
    QCOMPARE(editor->snapshot().id,ref.document.id); QVERIFY(editor->document()->isUndoAvailable());
    editor->undo(); QVERIFY(window.flush());
    QCOMPARE(editor->snapshot().blocks[0].runs[0].text,std::string("Jump"));
    window.findChild<QAction*>("redoBlockTransfer")->trigger();
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,"child"}).page_id,destination.document.id);
}
void DesktopTests::failed_transfer_undo_preserves_later_edits()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto source=w.create_page(db.id,"Source"), destination=w.create_page(db.id,"Destination");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto* choices=dialog->findChild<QComboBox*>("moveDestinationPage"); choices->setCurrentIndex(choices->findData(QString::fromStdString(destination.document.id)));
        dialog->accept();
    });
    window.findChild<QAction*>("moveBlockToPage")->trigger();
    QVERIFY(window.open_page(destination.document.id)); QTest::keyClicks(window.page_editor(),"New edit");
    QTimer::singleShot(0,&window,[]{ auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(box); box->accept(); });
    window.findChild<QAction*>("undoBlockTransfer")->trigger();
    QCOMPARE(w.load_page(destination.document.id).document.blocks[0].runs[0].text,std::string("New edit"));
    QCOMPARE(w.resolve_reference({ReferenceKind::Block,source.document.blocks[0].id}).page_id,destination.document.id);
    QVERIFY(window.findChild<QAction*>("undoBlockTransfer")->isEnabled());
    QCOMPARE(window.page_editor()->snapshot().blocks[0].runs[0].text,std::string("New edit"));
}
void DesktopTests::page_move_control_preserves_properties_and_focus()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"), b=w.create_database("Archive");
    const auto date=w.add_property(a.id,"Entry date",PropertyKind::Date);
    const auto page=w.create_page(a.id,"Entry",{{date.id,Date{"2024-02-29"}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id)); QTest::keyClicks(window.page_editor(),"Content");
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto* choices=dialog->findChild<QComboBox*>("moveDestinationDatabase"); choices->setCurrentIndex(choices->findData(QString::fromStdString(b.id))); dialog->accept();
    });
    window.findChild<QAction*>("movePage")->trigger();
    QCOMPARE(window.findChild<QComboBox*>("databases")->currentData().toString(),QString::fromStdString(b.id));
    QCOMPARE(window.page_editor()->snapshot().id,page.document.id); QTRY_COMPARE(QApplication::focusWidget(),window.page_editor());
    const auto moved=w.load_page(page.document.id);
    QCOMPARE(moved.document.blocks[0].runs[0].text,std::string("Content"));
    QCOMPARE(std::get<Date>(moved.properties.at(w.properties(b.id)[0].id)).iso,std::string("2024-02-29"));
    QCOMPARE(window.findChild<QTableWidget*>("databaseTable")->rowCount(),1);
    QVERIFY(window.flush());
}
void DesktopTests::unavailable_references_preserve_current_page_and_identity()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Source"), b=w.create_database("Target");
    auto target=w.create_page(b.id,"Target");
    const auto source=w.create_page(a.id,"Source",{},{{"source","",BlockKind::Paragraph,{{"Jump",false,false,Reference{ReferenceKind::Block,target.document.blocks[0].id}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    auto* editor=window.page_editor(); const auto target_id=QString::fromStdString(target.document.blocks[0].id);
    w.trash_page(target.document.id,true); editor->reference_activated(int(ReferenceKind::Block),target_id);
    QCOMPARE(editor->snapshot().id,source.document.id); QVERIFY(window.statusBar()->currentMessage().contains("page is in trash"));
    w.create_page(b.id,"Target"); w.trash_page(target.document.id,false); w.trash_database(b.id,true);
    editor->reference_activated(int(ReferenceKind::Block),target_id);
    QCOMPARE(editor->snapshot().id,source.document.id); QVERIFY(window.statusBar()->currentMessage().contains("database is in trash"));
    w.trash_database(b.id,false); window.refresh_databases(); QVERIFY(window.open_page(source.document.id));
    editor->reference_activated(int(ReferenceKind::Block),target_id);
    QCOMPARE(editor->snapshot().id,target.document.id); QCOMPARE(editor->current_block_id(),target_id);
    QVERIFY(window.open_page(source.document.id)); editor->reference_activated(int(ReferenceKind::Page),"unknown");
    QCOMPARE(editor->snapshot().id,source.document.id); QVERIFY(window.statusBar()->currentMessage().contains("unavailable"));
    QCOMPARE(editor->snapshot().blocks[0].runs[0].reference->target_id,target_id.toStdString());
}
void DesktopTests::failed_save_blocks_workspace_moves()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"); w.create_database("Archive");
    const auto source=w.create_page(a.id,"Source"); w.create_page(a.id,"Destination");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    sql(folder.filePath("nebula.sqlite3"),"CREATE TRIGGER fail_save BEFORE UPDATE ON pages BEGIN SELECT RAISE(ABORT,'injected save failure'); END;");
    QTest::keyClicks(window.page_editor(),"Pending");
    window.findChild<QAction*>("moveBlockToPage")->trigger(); window.findChild<QAction*>("movePage")->trigger();
    QVERIFY(window.findChild<QLabel*>("saveState")->text().startsWith("Save failed"));
    QVERIFY(!window.findChild<QAction*>("undoBlockTransfer")->isEnabled());
    QCOMPARE(window.page_editor()->snapshot().blocks[0].runs[0].text,std::string("Pending"));
    QCOMPARE(w.load_page(source.document.id).revision,source.revision);
    QCOMPARE(w.load_page(source.document.id).database_id,a.id);
    sql(folder.filePath("nebula.sqlite3"),"DROP TRIGGER fail_save;"); QVERIFY(window.flush());
}
void DesktopTests::typed_wiki_links_persist_page_heading_block_and_unresolved_targets()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto target=w.create_page(db.id,"Target",{},{{"target-heading","",BlockKind::Heading,{{"Meeting"}}},
        {"target-body","",BlockKind::Paragraph,{{"Body"}}}});
    const auto literal=w.create_page(db.id,"A#B|C");
    const auto source=w.create_page(db.id,"Source"); WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    auto* editor=window.page_editor();
    QTest::keyClicks(editor,R"([[Target|project]] [[Target#Meeting|notes]] [[Target#^target-body|body]] [[A\#B\|C|literal]] [[Future|later]])");
    QVERIFY(window.flush()); const auto saved=w.load_page(source.document.id);
    std::vector<Reference> references;
    std::string text; for (const auto& run:saved.document.blocks[0].runs) { text+=run.text; if (run.reference) references.push_back(*run.reference); }
    QCOMPARE(text,std::string("project notes body literal later")); QCOMPARE(references.size(),std::size_t(5));
    QCOMPARE(references[0].target_id,target.document.id); QCOMPARE(references[1].target_id,std::string("target-heading"));
    QCOMPARE(references[2].target_id,std::string("target-body")); QCOMPARE(references[3].target_id,literal.document.id); QVERIFY(references[4].target_id.empty());
    QCOMPARE(references[4].unresolved_name,std::string("Future")); QCOMPARE(w.pages(db.id).size(),std::size_t(3));
    QCOMPARE(w.backlinks(ReferenceKind::Page,target.document.id).size(),std::size_t(3));
    editor->undo(); QVERIFY(window.flush()); QVERIFY(editor->toPlainText().endsWith("[[Future|later]]"));
    editor->redo(); QVERIFY(window.flush()); QVERIFY(editor->toPlainText().endsWith("later"));
    QTest::keyClicks(editor," [[Target#Missing]]"); QVERIFY(editor->toPlainText().endsWith("[[Target#Missing]]"));
    QVERIFY(!editor->textCursor().hasSelection()); QVERIFY(window.flush());
    auto reopened=Workspace::open(path(folder.path())); QCOMPARE(reopened.backlinks(ReferenceKind::Block,"target-heading").size(),std::size_t(1));
}
void DesktopTests::wiki_completion_selects_a_specific_duplicate_target()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("First"), b=w.create_database("Second");
    w.create_page(a.id,"Duplicate"); const auto desired=w.create_page(b.id,"Duplicate"); const auto source=w.create_page(a.id,"Source");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id)); auto* editor=window.page_editor();
    QTest::keyClicks(editor,"[[Dup"); auto* completer=window.findChild<QCompleter*>("wikiCompleter"); QVERIFY(completer);
    auto* popup=completer->popup(); QTRY_VERIFY(popup->isVisible());
    QModelIndex desired_index;
    for (int row=0;row<popup->model()->rowCount();++row) {
        const auto index=popup->model()->index(row,0);
        if (index.data(Qt::UserRole+1).toString()==QString::fromStdString(desired.document.id)) desired_index=index;
    }
    QVERIFY(desired_index.isValid()); QVERIFY(desired_index.data().toString().contains("Second / Duplicate"));
    popup->setCurrentIndex(desired_index); QTest::keyClick(popup,Qt::Key_Return);
    QVERIFY(window.flush()); QCOMPARE(w.load_page(source.document.id).document.blocks[0].runs[0].reference->target_id,desired.document.id);
    QCOMPARE(editor->toPlainText(),QString("Duplicate"));
    editor->undo(); QVERIFY(editor->toPlainText().contains("[[Dup")); editor->redo();
}
void DesktopTests::ambiguous_wiki_links_require_explicit_choice_or_preserve_text()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("First"), b=w.create_database("Second");
    w.create_page(a.id,"Duplicate"); const auto desired=w.create_page(b.id,"Duplicate"); const auto source=w.create_page(a.id,"Source");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id)); auto* editor=window.page_editor();
    QTimer choose; bool saw_dialog=false; bool cancel=true;
    connect(&choose,&QTimer::timeout,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) return;
        saw_dialog=true;
        if (cancel) { dialog->reject(); return; }
        auto* tree=dialog->findChild<QTreeWidget*>("linkTargets"); QVERIFY(tree);
        QVERIFY(!dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
        dialog->findChild<QLineEdit*>("linkSearch")->setText("Second / Duplicate");
        for (int row=0;row<tree->topLevelItemCount();++row) {
            auto* item=tree->topLevelItem(row);
            if (item->data(0,Qt::UserRole).toString()==QString::fromStdString(desired.document.id)) { QVERIFY(!item->isHidden()); tree->setCurrentItem(item); }
        }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    choose.start(0); QTest::keyClicks(editor,"[[Duplicate]]"); choose.stop(); QVERIFY(saw_dialog);
    QCOMPARE(editor->toPlainText(),QString("[[Duplicate]]")); QVERIFY(window.flush());
    QVERIFY(!w.load_page(source.document.id).document.blocks[0].runs[0].reference);
    auto cursor=editor->textCursor(); cursor.select(QTextCursor::Document); editor->setTextCursor(cursor); QTest::keyClick(editor,Qt::Key_Backspace);
    cancel=false; saw_dialog=false; choose.start(0); QTest::keyClicks(editor,"[[Duplicate|chosen]]"); choose.stop(); QVERIFY(saw_dialog);
    QVERIFY(window.flush()); const auto saved=w.load_page(source.document.id).document.blocks[0].runs[0];
    QCOMPARE(saved.text,std::string("chosen")); QCOMPARE(saved.reference->target_id,desired.document.id);
    QCOMPARE(w.pages(a.id).size()+w.pages(b.id).size(),std::size_t(3));
}
void DesktopTests::keyboard_reference_follow_and_same_page_history_restore_positions()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto page=w.create_page(db.id,"Local navigation",{},{{"ref","",BlockKind::Paragraph,{{"Jump here",false,false,Reference{ReferenceKind::Block,"target-heading"}}}},
        {"between","",BlockKind::Paragraph,{{"Between"}}},{"target-heading","",BlockKind::Heading,{{"Heading"}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id)); auto* editor=window.page_editor();
    auto cursor=editor->textCursor(); cursor.setPosition(2); editor->setTextCursor(cursor);
    QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
    QCOMPARE(editor->current_block_id(),QString("target-heading")); QVERIFY(window.findChild<QAction*>("navigateBack")->isEnabled());
    QTest::keyClick(editor,Qt::Key_Left,Qt::AltModifier);
    QCOMPARE(editor->current_block_id(),QString("ref")); QCOMPARE(editor->textCursor().positionInBlock(),2);
    QTest::keyClick(editor,Qt::Key_Right,Qt::AltModifier); QCOMPARE(editor->current_block_id(),QString("target-heading"));
    QCOMPARE(w.load_page(page.document.id).revision,page.revision);
}
void DesktopTests::cross_database_history_preserves_selection_scroll_and_forward_branch()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("First"), b=w.create_database("Second");
    const auto source=w.create_page(a.id,"Long",{},long_page(90).blocks), target=w.create_page(b.id,"Target");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id)); auto* editor=window.page_editor();
    editor->go_to_block("block-49"); auto cursor=editor->textCursor(); const auto anchor=cursor.position()+3;
    editor->go_to_block("block-50"); const auto end=editor->textCursor().position()+8;
    cursor.setPosition(anchor); cursor.setPosition(end,QTextCursor::KeepAnchor); editor->setTextCursor(cursor);
    editor->verticalScrollBar()->setValue(1234); const auto original=editor->capture_position();
    auto* databases=window.findChild<QComboBox*>("databases"); databases->setCurrentIndex(databases->findData(QString::fromStdString(b.id)));
    QVERIFY(window.open_page(target.document.id)); QTest::keyClick(editor,Qt::Key_Left,Qt::AltModifier);
    QCOMPARE(editor->snapshot().id,source.document.id); QCOMPARE(editor->textCursor().anchor(),anchor); QCOMPARE(editor->textCursor().position(),end);
    QCOMPARE(editor->verticalScrollBar()->value(),original.scroll);
    QVERIFY(window.findChild<QAction*>("navigateForward")->isEnabled());
    const auto third=w.create_page(a.id,"Third"); QVERIFY(window.open_page(third.document.id));
    QVERIFY(!window.findChild<QAction*>("navigateForward")->isEnabled());
    window.findChild<QAction*>("navigateBack")->trigger(); QCOMPARE(editor->snapshot().id,source.document.id);
}
void DesktopTests::outgoing_and_backlink_lists_navigate_exact_blocks_with_keyboard()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto target=w.create_page(db.id,"Target",{},{{"target-heading","",BlockKind::Heading,{{"Meeting"}}}});
    const auto source=w.create_page(db.id,"Source",{},{{"intro","",BlockKind::Paragraph,{{"Introduction"}}},
        {"jump-source","",BlockKind::Paragraph,{{"Context before "},{"Meeting",false,false,Reference{ReferenceKind::Block,"target-heading"}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    auto* outgoing=window.findChild<QListWidget*>("outgoingLinks"); QCOMPARE(outgoing->count(),1);
    QVERIFY(outgoing->item(0)->text().contains("Context before")); QVERIFY(outgoing->item(0)->text().contains("Target"));
    outgoing->setCurrentRow(0); outgoing->setFocus(); QTest::keyClick(outgoing,Qt::Key_Return);
    QCOMPARE(window.page_editor()->snapshot().id,target.document.id); QCOMPARE(window.page_editor()->current_block_id(),QString("target-heading"));
    auto* backlinks=window.findChild<QListWidget*>("backlinks"); QCOMPARE(backlinks->count(),1);
    QVERIFY(backlinks->item(0)->text().contains("Context before")); QVERIFY(backlinks->item(0)->toolTip().contains("jump-source"));
    backlinks->setCurrentRow(0); backlinks->setFocus(); QTest::keyClick(backlinks,Qt::Key_Return);
    QCOMPARE(window.page_editor()->snapshot().id,source.document.id); QCOMPARE(window.page_editor()->current_block_id(),QString("jump-source"));
    QTest::keyClick(window.page_editor(),Qt::Key_Left,Qt::AltModifier);
    QCOMPARE(window.page_editor()->snapshot().id,target.document.id); QCOMPARE(window.page_editor()->current_block_id(),QString("target-heading"));
}
void DesktopTests::history_refuses_trash_and_failed_saves_without_losing_entries()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    const auto source=w.create_page(db.id,"Source"), target=w.create_page(db.id,"Target");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id)); QVERIFY(window.open_page(target.document.id));
    auto* editor=window.page_editor(); w.trash_page(source.document.id,true);
    window.findChild<QAction*>("navigateBack")->trigger(); QCOMPARE(editor->snapshot().id,target.document.id);
    QVERIFY(window.findChild<QAction*>("navigateBack")->isEnabled()); QVERIFY(window.statusBar()->currentMessage().contains("trash"));
    w.trash_page(source.document.id,false);
    sql(folder.filePath("nebula.sqlite3"),"CREATE TRIGGER fail_navigation_save BEFORE UPDATE ON pages BEGIN SELECT RAISE(ABORT,'injected save failure'); END;");
    QTest::keyClicks(editor,"Pending"); QTest::keyClick(editor,Qt::Key_Left,Qt::AltModifier);
    QCOMPARE(editor->snapshot().id,target.document.id); QVERIFY(window.findChild<QAction*>("navigateBack")->isEnabled());
    QCOMPARE(editor->toPlainText(),QString("Pending"));
    sql(folder.filePath("nebula.sqlite3"),"DROP TRIGGER fail_navigation_save;");
    QTest::keyClick(editor,Qt::Key_Left,Qt::AltModifier); QCOMPARE(editor->snapshot().id,source.document.id);
    QTest::keyClick(editor,Qt::Key_Right,Qt::AltModifier); QCOMPARE(editor->snapshot().id,target.document.id);
    QCOMPARE(editor->toPlainText(),QString("Pending"));
}
void DesktopTests::default_and_custom_labels_follow_renames_without_retargeting()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes");
    auto target=w.create_page(db.id,"Original");
    target.document.blocks[0].runs={{"Cached",false,false,Reference{ReferenceKind::Page,target.document.id,"","",false}}}; w.save_page(target);
    const auto source=w.create_page(db.id,"Source",{},{{"source","",BlockKind::Paragraph,{
        {"Cached",false,false,Reference{ReferenceKind::Page,target.document.id,"","",false}},
        {" fixed",true,false,Reference{ReferenceKind::Page,target.document.id}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    QCOMPARE(window.page_editor()->toPlainText(),QString("Original fixed"));
    QVERIFY(window.open_page(target.document.id)); auto* title=window.findChild<QLineEdit*>("pageTitle");
    title->setFocus(); title->selectAll(); QTest::keyClicks(title,"Renamed"); QVERIFY(window.flush());
    QCOMPARE(w.load_page(target.document.id).title,std::string("Renamed")); QCOMPARE(window.page_editor()->toPlainText(),QString("Renamed"));
    auto* editor=window.page_editor(); editor->setFocus(); editor->undo(); QVERIFY(window.flush());
    QCOMPARE(title->text(),editor->page_title()); QCOMPARE(w.load_page(target.document.id).title,title->text().toStdString());
    editor->redo(); QVERIFY(window.flush()); QCOMPARE(title->text(),QString("Renamed"));
    QVERIFY(window.open_page(source.document.id)); QCOMPARE(editor->toPlainText(),QString("Renamed fixed"));
    QVERIFY(window.findChild<QListWidget*>("outgoingLinks")->item(0)->text().startsWith("Renamed → Renamed"));
    const auto page=editor->snapshot(); QVERIFY(!page.blocks[0].runs[0].reference->custom_label); QVERIFY(page.blocks[0].runs[1].reference->custom_label);
    QCOMPARE(page.blocks[0].runs[0].reference->target_id,target.document.id);
}
void DesktopTests::unresolved_creation_chooses_database_and_undo_keeps_created_page()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"), b=w.create_database("Projects");
    const auto source=w.create_page(a.id,"Source"), same_name=w.create_page(b.id,"Future");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id)); auto* editor=window.page_editor();
    QTest::keyClicks(editor,"[[FutureName]]"); QVERIFY(window.flush());
    QTimer::singleShot(0,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); QCOMPARE(dialog->objectName(),QString("resolveUnresolved"));
        auto* databases=dialog->findChild<QComboBox*>("targetDatabase"); databases->setCurrentIndex(databases->findData(QString::fromStdString(b.id)));
        dialog->findChild<QLineEdit*>("targetPageTitle")->setText("Future"); dialog->findChild<QPushButton*>("createReferenceTarget")->click();
    });
    QTest::keyClick(editor,Qt::Key_Return,Qt::ControlModifier);
    const auto reference=editor->snapshot().blocks[0].runs[0].reference; QVERIFY(reference); QVERIFY(!reference->target_id.empty());
    QVERIFY(reference->target_id!=same_name.document.id); QCOMPARE(w.load_page(reference->target_id).database_id,b.id);
    QCOMPARE(editor->toPlainText(),QString("Future")); QCOMPARE(w.pages(b.id).size(),std::size_t(2));
    QCOMPARE(w.load_page(source.document.id).document.blocks[0].runs[0].reference->target_id,reference->target_id);
    editor->undo(); QVERIFY(window.flush()); QCOMPARE(editor->toPlainText(),QString("FutureName"));
    QVERIFY(w.load_page(source.document.id).document.blocks[0].runs[0].reference->target_id.empty()); QCOMPARE(w.pages(b.id).size(),std::size_t(2));
    editor->redo(); QVERIFY(window.flush()); QCOMPARE(w.backlinks(ReferenceKind::Page,reference->target_id).size(),std::size_t(1));
}
void DesktopTests::unresolved_existing_choice_replaces_only_the_selected_reference()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"), b=w.create_database("Projects");
    const auto target=w.create_page(b.id,"Future");
    const auto source=w.create_page(a.id,"Source",{},{{"source","",BlockKind::Paragraph,{
        {"Future",false,false,Reference{ReferenceKind::Page,"","Future",a.id,false}},{" and "},
        {"read notes",true,false,Reference{ReferenceKind::Page,"","Future",a.id,true}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    QTimer choose;
    connect(&choose,&QTimer::timeout,&window,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) return;
        if (dialog->objectName()=="resolveUnresolved") { dialog->findChild<QPushButton*>("chooseReferenceTarget")->click(); return; }
        auto* tree=dialog->findChild<QTreeWidget*>("linkTargets"); if (!tree) return;
        QCOMPARE(dialog->findChild<QLineEdit*>("linkLabel")->text(),QString("read notes"));
        for (int row=0;row<tree->topLevelItemCount();++row) if (tree->topLevelItem(row)->data(0,Qt::UserRole).toString()==QString::fromStdString(target.document.id)) tree->setCurrentItem(tree->topLevelItem(row));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    auto* outgoing=window.findChild<QListWidget*>("outgoingLinks"); QCOMPARE(outgoing->count(),2); outgoing->setCurrentRow(1); outgoing->setFocus();
    choose.start(0); QTest::keyClick(outgoing,Qt::Key_Return); choose.stop(); QVERIFY(window.flush());
    const auto runs=w.load_page(source.document.id).document.blocks[0].runs;
    QVERIFY(runs[0].reference->target_id.empty()); QCOMPARE(runs[2].reference->target_id,target.document.id);
    QCOMPARE(runs[2].text,std::string("read notes")); QVERIFY(runs[2].bold); QVERIFY(runs[2].reference->custom_label);
    window.page_editor()->undo(); QVERIFY(window.flush()); QVERIFY(w.load_page(source.document.id).document.blocks[0].runs[2].reference->target_id.empty());
}
void DesktopTests::unresolved_creation_failure_keeps_reference_and_no_orphan_page()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto a=w.create_database("Notes"), b=w.create_database("Projects");
    const auto source=w.create_page(a.id,"Source",{},{{"source","",BlockKind::Paragraph,{{"Future",false,false,Reference{ReferenceKind::Page,"","Future",a.id,false}}}}});
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(source.document.id));
    sql(folder.filePath("nebula.sqlite3"),"CREATE TRIGGER fail_create_link BEFORE INSERT ON links BEGIN SELECT RAISE(ABORT,'injected link failure'); END;");
    QTimer choose; bool saw_error=false;
    connect(&choose,&QTimer::timeout,&window,[&]{
        if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { saw_error=true; box->accept(); return; }
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog || dialog->objectName()!="resolveUnresolved") return;
        auto* databases=dialog->findChild<QComboBox*>("targetDatabase"); databases->setCurrentIndex(databases->findData(QString::fromStdString(b.id)));
        dialog->findChild<QPushButton*>("createReferenceTarget")->click();
    });
    choose.start(0); QTest::keyClick(window.page_editor(),Qt::Key_Return,Qt::ControlModifier); choose.stop();
    QVERIFY(saw_error); QVERIFY(w.pages(b.id).empty()); QVERIFY(window.page_editor()->snapshot().blocks[0].runs[0].reference->target_id.empty());
    QCOMPARE(w.load_page(source.document.id).revision,source.revision); QCOMPARE(window.page_editor()->toPlainText(),QString("Future"));
}
void DesktopTests::managed_file_import_export_undo_and_reopen()
{
    QTemporaryDir folder; const auto original=folder.filePath(QString::fromUtf8(u8"資料.txt"));
    { QFile out(original); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("managed bytes\n"); }
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Files");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    QTimer::singleShot(0,&window,[&]{ auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->selectFile(original); QMetaObject::invokeMethod(dialog,"accept"); });
    window.findChild<QAction*>("importFile")->trigger();
    auto* editor=window.page_editor(); const auto imported=w.load_page(page.document.id);
    QCOMPARE(imported.document.blocks.size(),std::size_t(3)); const auto file=imported.document.blocks[1].attachment_id; QVERIFY(!file.empty());
    QCOMPARE(window.findChild<QLabel*>("saveState")->text(),QString("Saved locally")); QVERIFY(QFile::remove(original));
    QVERIFY(editor->go_to_block(QString::fromStdString(imported.document.blocks[1].id))); QVERIFY(window.findChild<QAction*>("exportAttachment")->isEnabled());
    const auto exported=folder.filePath("exported.txt");
    QTimer::singleShot(0,&window,[&]{ auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->selectFile(exported); QMetaObject::invokeMethod(dialog,"accept"); });
    window.findChild<QAction*>("exportAttachment")->trigger(); QCOMPARE(contents(exported),QByteArray("managed bytes\n"));
    editor->undo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks.size(),std::size_t(1));
    QVERIFY(w.audit_attachments().empty()); QVERIFY(w.attachment(file));
    editor->redo(); QVERIFY(window.flush()); QCOMPARE(w.load_page(page.document.id).document.blocks[1].attachment_id,file);
    auto reopened=Workspace::open(path(folder.filePath("workspace"))); QCOMPARE(reopened.load_page(page.document.id).document.blocks[1].attachment_id,file);
    QCOMPARE(contents(QString::fromStdString(reopened.attachment_path(file).u8string())),QByteArray("managed bytes\n"));
}
void DesktopTests::managed_image_preview_uses_copied_bytes_and_reports_missing_file()
{
    QTemporaryDir folder; const auto original=folder.filePath("image.png"); QImage picture(80,60,QImage::Format_RGB32); picture.fill(QColor("#ee2020")); QVERIFY(picture.save(original));
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Images");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    QTimer::singleShot(0,&window,[&]{ auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->selectFile(original); QMetaObject::invokeMethod(dialog,"accept"); });
    window.findChild<QAction*>("importImage")->trigger(); const auto saved=w.load_page(page.document.id);
    QCOMPARE(saved.document.blocks[1].kind,BlockKind::Image); const auto id=saved.document.blocks[1].attachment_id; QVERIFY(!id.empty());
    QVERIFY(QFile::remove(original)); auto* editor=window.page_editor(); QVERIFY(editor->go_to_block(QString::fromStdString(saved.document.blocks[1].id)));
    auto red_pixels=[&]{
        const auto image=editor->viewport()->grab().toImage(); int count=0;
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) { const auto c=image.pixelColor(x,y); if (c.red()>200 && c.green()<60 && c.blue()<60) ++count; }
        return count;
    };
    QTRY_VERIFY(red_pixels()>500); QVERIFY(QFile::remove(QString::fromStdString(w.attachment_path(id).u8string()))); editor->viewport()->update();
    QTRY_COMPARE(red_pixels(),0); QVERIFY(!w.audit_attachments().empty());
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto* issues=dialog->findChild<QListWidget*>("attachmentIssues"); QVERIFY(issues && issues->count()>0); issues->setCurrentRow(0);
        QVERIFY(!dialog->findChild<QPushButton*>("removeUnusedAttachment")->isEnabled()); dialog->accept(); });
    window.findChild<QAction*>("auditAttachments")->trigger(); QCOMPARE(editor->snapshot().blocks[1].attachment_id,id);
}
void DesktopTests::import_cancel_and_invalid_image_preserve_page()
{
    QTemporaryDir folder; const auto original=folder.filePath("text.txt"); { QFile out(original); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("not an image"); }
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Source");
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    QTimer::singleShot(0,&window,[]{ auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject(); });
    window.findChild<QAction*>("importFile")->trigger(); QCOMPARE(w.load_page(page.document.id).revision,page.revision); QVERIFY(w.attachments().empty());
    QTimer choose; bool saw_error=false;
    connect(&choose,&QTimer::timeout,&window,[&]{
        if (auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { saw_error=true; box->accept(); }
        else if (auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) { dialog->selectFile(original); QMetaObject::invokeMethod(dialog,"accept"); }
    });
    choose.start(0); window.findChild<QAction*>("importImage")->trigger(); choose.stop();
    QVERIFY(saw_error); QVERIFY(w.attachments().empty()); QCOMPARE(w.load_page(page.document.id).revision,page.revision); QCOMPARE(contents(original),QByteArray("not an image"));
}
void DesktopTests::attachment_audit_control_removes_only_unused_files()
{
    QTemporaryDir folder; auto w=Workspace::open(path(folder.path())); const auto db=w.create_database("Notes"); const auto page=w.create_page(db.id,"Keep");
    const auto orphan=folder.filePath("attachments/.import-abandoned"); { QFile out(orphan); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("orphan"); }
    WorkspaceWindow window(w); show_window(window); QVERIFY(window.open_page(page.document.id));
    QTimer::singleShot(0,&window,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); QVERIFY(dialog); auto* issues=dialog->findChild<QListWidget*>("attachmentIssues"); QCOMPARE(issues->count(),1);
        issues->setCurrentRow(0); auto* remove=dialog->findChild<QPushButton*>("removeUnusedAttachment"); QVERIFY(remove->isEnabled()); remove->click(); QCOMPARE(issues->count(),0); dialog->accept();
    });
    window.findChild<QAction*>("auditAttachments")->trigger(); QVERIFY(!QFile::exists(orphan)); QCOMPARE(w.load_page(page.document.id).document.id,page.document.id);
}
void DesktopTests::journal_with_image_and_file_restores_into_fresh_window()
{
    QTemporaryDir folder; const auto image_source=folder.filePath("journal.png"), file_source=folder.filePath("journal.txt");
    QImage picture(50,40,QImage::Format_RGB32); picture.fill(QColor("#2456ab")); QVERIFY(picture.save(image_source));
    { QFile out(file_source); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("journal attachment"); }
    auto w=Workspace::open(path(folder.filePath("workspace"))); const auto db=w.create_database("Journal"); const auto date=w.add_property(db.id,"Entry date",PropertyKind::Date);
    auto page=w.create_page(db.id,"2024-03-10",{{date.id,Date{"2024-03-10"}}});
    const auto image=w.import_attachment(page,page.document.blocks[0].id,path(image_source),true);
    const auto file=w.import_attachment(page,image.following,path(file_source),false);
    QVERIFY(QFile::remove(image_source)); QVERIFY(QFile::remove(file_source));
    w.backup_to(path(folder.filePath("backup"))); auto restored=Workspace::restore(path(folder.filePath("backup")),path(folder.filePath("restored")));
    const auto recovered=restored.load_page(page.document.id); QCOMPARE(std::get<Date>(recovered.properties.at(date.id)).iso,std::string("2024-03-10"));
    QCOMPARE(contents(QString::fromStdString(restored.attachment_path(file.file.id).u8string())),QByteArray("journal attachment"));
    QImage decoded(QString::fromStdString(restored.attachment_path(image.file.id).u8string())); QVERIFY(!decoded.isNull()); QCOMPARE(decoded.pixelColor(1,1),QColor("#2456ab"));
    WorkspaceWindow window(restored); show_window(window); QVERIFY(window.open_page(page.document.id));
    QCOMPARE(window.findChild<QLineEdit*>("pageTitle")->text(),QString("2024-03-10"));
    QVERIFY(window.page_editor()->go_to_block(QString::fromStdString(image.card))); QCOMPARE(window.page_editor()->current_attachment_id(),image.file.id);
    QVERIFY(window.page_editor()->go_to_block(QString::fromStdString(file.card))); QVERIFY(window.findChild<QAction*>("exportAttachment")->isEnabled());
    QVERIFY(restored.audit_attachments().empty());
}
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
