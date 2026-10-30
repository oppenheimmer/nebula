// Window-level checks for the 2026-10-30 review suggestions (SUG-xx in ROADMAP.md).
#include "nebula/app/link_target_tree.hpp"
#include "nebula/app/termination.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/storage/workspace.hpp"
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <csignal>
#include <sqlite3.h>
#include <sys/stat.h>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& value)
{
    return std::filesystem::u8path(value.toUtf8().constData());
}
void sql(const QString& filename, const char* command)
{
    sqlite3* raw = nullptr;
    sqlite3_open(filename.toUtf8().constData(), &raw);
    std::unique_ptr<sqlite3, DatabaseCloser> database(raw);
    if (sqlite3_exec(raw, command, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Test database setup failed");
}
void show_window(QWidget& window)
{
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QApplication::processEvents();
}
// Answers the next modal dialog; records its type/name so tests can assert on it.
void answer(QWidget* context, std::function<void(QWidget*)> respond)
{
    QTimer::singleShot(0, context, [respond] { respond(QApplication::activeModalWidget()); });
}
void put(const QString& file, const QByteArray& data)
{
    QFile out(file);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(data);
}
} // namespace

class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QUrl last;
public slots:
    void open(const QUrl& url) { last = url; }
};

class ReviewDesktopTests : public QObject {
    Q_OBJECT
private slots:
    void folder_dialog_refuses_backups_and_confirms_unrelated_folders();
    void messages_and_status_never_render_data_as_html();
    void attachments_open_as_read_only_named_copies();
    void conflicting_edits_can_be_kept_as_a_new_page();
    void empty_trash_requires_confirmation_and_purges();
    void views_and_properties_are_managed_from_the_window();
    void autosave_keeps_an_open_table_cell_editor();
    void merged_blocks_do_not_fill_restore();
    void termination_signals_reach_the_event_loop();
    void integrity_check_reports_from_the_window();
    void journal_bootstrap_is_complete();
    void link_picker_batches_pages_blocks_and_searches_all_rows();
    void link_picker_links_pending_blocks_from_the_window();
};

void ReviewDesktopTests::folder_dialog_refuses_backups_and_confirms_unrelated_folders()
{
    QTemporaryDir home;
    QDir root(home.path());
    QVERIFY(root.mkpath("backup"));
    put(home.filePath("backup/backup-manifest.json"), "{}");
    QVERIFY(root.mkpath("documents"));
    put(home.filePath("documents/letter.txt"), "keep");
    QCOMPARE(classify_workspace_folder(home.filePath("backup")), FolderKind::Backup);
    QCOMPARE(classify_workspace_folder(home.filePath("documents")), FolderKind::Other);
    QCOMPARE(classify_workspace_folder(home.filePath("missing")), FolderKind::New);
    QString asked;
    bool allow = false;
    WorkspaceDialog dialog(home.path(), nullptr, {}, [&](const QString& folder) {
        asked = folder;
        return allow;
    });
    dialog.set_directory(home.filePath("backup"));
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(dialog.findChild<QLabel*>("workspaceError")->text().contains("backup"));
    dialog.set_directory(home.filePath("documents"));
    dialog.accept();
    QCOMPARE(asked, home.filePath("documents"));
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    allow = true;
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
}
void ReviewDesktopTests::messages_and_status_never_render_data_as_html()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    WorkspaceWindow window(w);
    show_window(window);
    QCOMPARE(window.findChild<QLabel*>("saveState")->textFormat(), Qt::PlainText);
    Qt::TextFormat format = Qt::RichText;
    answer(&window, [](QWidget* dialog) {
        auto* input = qobject_cast<QInputDialog*>(dialog);
        QVERIFY(input);
        input->setTextValue("<b>not a date</b>");
        input->accept();
    });
    QTimer::singleShot(50, &window, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(box);
        format = box->textFormat();
        box->accept();
    });
    window.findChild<QAction*>("newJournalEntry")->trigger();
    QCOMPARE(format, Qt::PlainText);
}
void ReviewDesktopTests::attachments_open_as_read_only_named_copies()
{
    QTemporaryDir folder;
    const auto original = folder.filePath("report.pdf");
    put(original, "managed bytes");
    auto w = Workspace::open(path(folder.filePath("workspace")));
    auto page = w.create_page(w.create_database("Notes").id, "Files");
    const auto imported = w.import_attachment(page, page.document.blocks[0].id, path(original), false);
    UrlCatcher catcher;
    QDesktopServices::setUrlHandler("file", &catcher, "open");
    {
        WorkspaceWindow window(w);
        show_window(window);
        QVERIFY(window.open_page(page.document.id));
        QVERIFY(window.page_editor()->go_to_block(QString::fromStdString(imported.card)));
        window.findChild<QAction*>("openAttachment")->trigger();
        const auto opened = catcher.last.toLocalFile();
        QVERIFY(!opened.isEmpty());
        QVERIFY(!opened.contains("/workspace/attachments/"));
        QCOMPARE(QFileInfo(opened).fileName(), QString("report.pdf"));
        QFile copy(opened);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.readAll(), QByteArray("managed bytes"));
        struct stat status{};
        QCOMPARE(::stat(opened.toUtf8().constData(), &status), 0);
        QCOMPARE(status.st_mode & 0777, 0400u);
        QVERIFY(w.audit_attachments().empty());
        catcher.last = QUrl::fromLocalFile(opened);
    }
    QVERIFY(!QFile::exists(catcher.last.toLocalFile())); // Copies are removed with the window.
    QDesktopServices::unsetUrlHandler("file");
}
void ReviewDesktopTests::conflicting_edits_can_be_kept_as_a_new_page()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Shared");
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(page.document.id));
    page.title = "Changed elsewhere";
    w.save_page(page);
    QTest::keyClicks(window.page_editor(), "My draft");
    QVERIFY(!window.flush());
    auto* keep = window.findChild<QAction*>("saveConflictCopy");
    QVERIFY(keep->isEnabled());
    keep->trigger();
    QCOMPARE(w.pages(db.id).size(), std::size_t(2));
    QCOMPARE(w.load_page(page.document.id).title, std::string("Changed elsewhere"));
    const auto copy = window.page_editor()->snapshot();
    QVERIFY(copy.id != page.document.id);
    QCOMPARE(copy.blocks[0].runs[0].text, std::string("My draft"));
    QVERIFY(copy.blocks[0].id != page.document.blocks[0].id);
    QCOMPARE(w.load_page(copy.id).title, std::string("Shared (conflicting copy)"));
    QVERIFY(!keep->isEnabled());
    QVERIFY(window.close());
}
void ReviewDesktopTests::empty_trash_requires_confirmation_and_purges()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    const auto gone = w.create_page(db.id, "Gone");
    const auto keep = w.create_page(db.id, "Keep");
    w.trash_page(gone.document.id, true);
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(keep.document.id));
    auto run = [&](bool yes) {
        answer(&window, [&window, yes](QWidget* dialog) {
            QVERIFY(dialog);
            QTimer::singleShot(0, &window, [yes] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                QVERIFY(box);
                QCOMPARE(box->objectName(), QString("confirmEmptyTrash"));
                box->button(yes ? QMessageBox::Yes : QMessageBox::Cancel)->click();
            });
            dialog->findChild<QPushButton*>("emptyTrash")->click();
            if (auto* still = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
                still->accept();
        });
        window.findChild<QAction*>("restoreTrash")->trigger();
    };
    run(false);
    QCOMPARE(w.page_summaries(true).size(), std::size_t(2));
    run(true);
    QCOMPARE(w.page_summaries(true).size(), std::size_t(1));
    QCOMPARE(window.page_editor()->snapshot().id, keep.document.id);
    QVERIFY(window.statusBar()->currentMessage().contains("Permanently deleted 1 pages"));
}
void ReviewDesktopTests::views_and_properties_are_managed_from_the_window()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Alpha");
    const auto mood = w.add_property(db.id, "Mood", PropertyKind::Text);
    auto page = w.create_page(db.id, "Day", {{mood.id, std::string("calm")}});
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(page.document.id));
    answer(&window, [&](QWidget* first) {
        auto* choose = qobject_cast<QInputDialog*>(first);
        QVERIFY(choose);
        choose->setTextValue("Mood");
        QTimer::singleShot(0, &window, [] {
            auto* name = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            QVERIFY(name);
            name->setTextValue("Feeling");
            name->accept();
        });
        choose->accept();
    });
    window.findChild<QAction*>("renameProperty")->trigger();
    QCOMPARE(w.properties(db.id)[0].name, std::string("Feeling"));
    answer(&window, [&](QWidget* first) {
        auto* choose = qobject_cast<QInputDialog*>(first);
        QVERIFY(choose);
        choose->setTextValue("Feeling");
        QTimer::singleShot(0, &window, [] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->button(QMessageBox::Yes)->click();
        });
        choose->accept();
    });
    window.findChild<QAction*>("deleteProperty")->trigger();
    QVERIFY(w.properties(db.id).empty());
    QVERIFY(w.load_page(page.document.id).properties.empty());
    QCOMPARE(window.page_editor()->snapshot().id, page.document.id);
    QCOMPARE(w.views(db.id).size(), std::size_t(1));
    answer(&window, [](QWidget* dialog) {
        auto* box = qobject_cast<QMessageBox*>(dialog);
        QVERIFY(box);
        box->button(QMessageBox::Yes)->click();
    });
    window.findChild<QAction*>("deleteView")->trigger();
    QCOMPARE(w.views(db.id).size(), std::size_t(1)); // Switching recreates the default "All entries" view.
    QCOMPARE(w.views(db.id)[0].name, std::string("All entries"));
}
void ReviewDesktopTests::autosave_keeps_an_open_table_cell_editor()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    const auto score = w.add_property(db.id, "Score", PropertyKind::Number);
    const auto page = w.create_page(db.id, "Row");
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(page.document.id));
    QTest::keyClicks(window.page_editor(), "body text");
    auto* table = window.findChild<QTableWidget*>("databaseTable");
    table->editItem(table->item(0, 1));
    QTRY_VERIFY(qobject_cast<QLineEdit*>(QApplication::focusWidget()));
    auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    editor->setText("4,5");
    QVERIFY(window.flush());
    QCOMPARE(qobject_cast<QLineEdit*>(QApplication::focusWidget()), editor); // The cell editor survived the save.
    QTest::keyClick(editor, Qt::Key_Return);
    auto saved = [&] {
        const auto values = w.load_page(page.document.id).properties;
        const auto found = values.find(score.id);
        return found == values.end() ? -1.0 : std::get<double>(found->second);
    };
    QTRY_COMPARE(saved(), 4.5);
    QCOMPARE(w.load_page(page.document.id).document.blocks[0].runs[0].text, std::string("body text"));
}
void ReviewDesktopTests::merged_blocks_do_not_fill_restore()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    const auto db = w.create_database("Notes");
    const auto page =
        w.create_page(db.id, "Merge", {},
                      {{"a", "", BlockKind::Paragraph, {{"first"}}}, {"b", "", BlockKind::Paragraph, {{"second"}}}});
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(page.document.id));
    QVERIFY(window.page_editor()->go_to_block("b"));
    QTest::keyClick(window.page_editor(), Qt::Key_Backspace);
    QVERIFY(window.flush());
    QVERIFY(w.deleted_blocks().empty());
    QCOMPARE(w.load_page(page.document.id).document.blocks.size(), std::size_t(1));
    QTest::keyClick(window.page_editor(), Qt::Key_Return);
    QTest::keyClicks(window.page_editor(), "third");
    QTextCursor all(window.page_editor()->document());
    all.select(QTextCursor::Document);
    window.page_editor()->setTextCursor(all);
    QTest::keyClick(window.page_editor(), Qt::Key_Delete);
    QVERIFY(window.flush());
    QCOMPARE(w.deleted_blocks().size(), std::size_t(1)); // Real deletions still archive.
}
void ReviewDesktopTests::termination_signals_reach_the_event_loop()
{
    int received = 0;
    install_termination_handler([&](int signal) { received = signal; });
    std::raise(SIGTERM);
    QTRY_COMPARE(received, int(SIGTERM));
    std::signal(SIGTERM, SIG_DFL);
}
void ReviewDesktopTests::integrity_check_reports_from_the_window()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    WorkspaceWindow window(w);
    show_window(window);
    QString text;
    answer(&window, [&](QWidget* dialog) {
        auto* box = qobject_cast<QMessageBox*>(dialog);
        QVERIFY(box);
        text = box->text();
        box->accept();
    });
    window.findChild<QAction*>("checkIntegrity")->trigger();
    QVERIFY(text.contains("passed"));
    sql(folder.filePath("nebula.sqlite3"), "PRAGMA foreign_keys=OFF; INSERT INTO pages(id,database_id,title,revision) "
                                           "VALUES('orphan','missing','Orphan',1)");
    answer(&window, [&](QWidget* dialog) {
        auto* box = qobject_cast<QMessageBox*>(dialog);
        QVERIFY(box);
        text = box->text();
        box->accept();
    });
    window.findChild<QAction*>("checkIntegrity")->trigger();
    QVERIFY(text.contains("Problems were found"));
}
void ReviewDesktopTests::journal_bootstrap_is_complete()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    {
        WorkspaceWindow window(w);
    }
    const auto databases = w.databases();
    QCOMPARE(databases.size(), std::size_t(1));
    QCOMPARE(w.properties(databases[0].id).size(), std::size_t(3));
    QCOMPARE(w.views(databases[0].id)[0].visible_properties.size(), std::size_t(3));
}

void ReviewDesktopTests::link_picker_batches_pages_blocks_and_searches_all_rows()
{
    std::vector<LinkTargetRow> rows;
    for (int p = 0; p < 205; ++p) {
        const auto id = std::to_string(p);
        rows.push_back({ReferenceKind::Page, id, "Duplicate", id, "Duplicate", "Notes", false});
        for (int b = 0; b < (p == 204 ? 251 : 1); ++b)
            rows.push_back({ReferenceKind::Block, id + "-" + std::to_string(b), b == 250 ? u8"日本語 needle" : "Body",
                            id, "Duplicate", "Notes", b == 250});
    }
    LinkTargetTree tree(std::move(rows));
    show_window(tree);
    QCOMPARE(tree.topLevelItemCount(), 101);
    QCOMPARE(tree.topLevelItem(0)->childCount(), 0);
    tree.setCurrentItem(tree.topLevelItem(100));
    QTest::keyClick(&tree, Qt::Key_Return);
    QTRY_COMPARE(tree.topLevelItemCount(), 201);
    tree.setCurrentItem(tree.topLevelItem(200));
    QTest::keyClick(&tree, Qt::Key_Return);
    QTRY_COMPARE(tree.topLevelItemCount(), 205);
    auto* page = tree.topLevelItem(204);
    tree.expandItem(page);
    QCOMPARE(page->childCount(), 101);
    tree.setCurrentItem(page->child(100));
    QTest::keyClick(&tree, Qt::Key_Return);
    QTRY_COMPARE(page->childCount(), 201);
    tree.setCurrentItem(page->child(200));
    QTest::keyClick(&tree, Qt::Key_Return);
    QTRY_COMPARE(page->childCount(), 251);
    tree.search(QString::fromUtf8(u8"Notes 日本語 needle"));
    QCOMPARE(tree.topLevelItemCount(), 1);
    page = tree.topLevelItem(0);
    tree.expandItem(page);
    QCOMPARE(page->childCount(), 1);
    QCOMPARE(page->child(0)->data(0, Qt::UserRole).toString(), QString("204-250"));
    tree.search("missing");
    QCOMPARE(tree.topLevelItemCount(), 0);
    tree.search({});
    // Queue a continuation, then replace the query before that callback runs.
    tree.setCurrentItem(tree.topLevelItem(100));
    QTest::keyClick(&tree, Qt::Key_Return);
    tree.search("missing");
    QApplication::processEvents();
    QCOMPARE(tree.topLevelItemCount(), 0);
}
void ReviewDesktopTests::link_picker_links_pending_blocks_from_the_window()
{
    QTemporaryDir folder;
    auto w = Workspace::open(path(folder.path()));
    auto page = w.create_page(w.create_database("Notes").id, "Source");
    WorkspaceWindow window(w);
    show_window(window);
    QVERIFY(window.open_page(page.document.id));
    auto* editor = window.page_editor();
    QTest::keyClicks(editor, "Unsaved target");
    const auto block = editor->snapshot().blocks.front().id;
    bool selected = false;
    answer(&window, [&](QWidget* dialog) {
        QVERIFY(dialog);
        auto* tree = dialog->findChild<QTreeWidget*>("linkTargets");
        QVERIFY(tree);
        auto* search = dialog->findChild<QLineEdit*>("linkSearch");
        QVERIFY(search);
        search->setText("Unsaved target");
        QCOMPARE(tree->topLevelItemCount(), 1);
        auto* parent = tree->topLevelItem(0);
        tree->expandItem(parent);
        QCOMPARE(parent->childCount(), 1);
        QCOMPARE(parent->child(0)->data(0, Qt::UserRole).toString(), QString::fromStdString(block));
        tree->setCurrentItem(parent->child(0));
        dialog->findChild<QLineEdit*>("linkLabel")->setText("Jump");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        selected = true;
    });
    window.findChild<QAction*>("insertLink")->trigger();
    QVERIFY(selected);
    QVERIFY(window.flush());
    const auto saved = w.load_page(page.document.id);
    QVERIFY(saved.document.blocks.front().runs.back().reference);
    QCOMPARE(saved.document.blocks.front().runs.back().reference->target_id, block);
    editor->undo();
    QVERIFY(window.flush());
    QCOMPARE(editor->toPlainText(), QString("Unsaved target"));
    editor->redo();
    QVERIFY(window.flush());
    QCOMPARE(w.backlinks(ReferenceKind::Block, block).size(), std::size_t(1));
}

QTEST_MAIN(ReviewDesktopTests)
#include "review_desktop_tests.moc"
