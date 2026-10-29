#include "nebula/app/prototype_window.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/storage/workspace.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <sqlite3.h>

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

QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
