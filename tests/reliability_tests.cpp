#include "nebula/app/workspace_window.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <sqlite3.h>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& text)
{
    return std::filesystem::u8path(text.toUtf8().constData());
}
QString worker()
{
    return QCoreApplication::applicationDirPath() + "/nebula_reliability_worker";
}
void put(const QString& name, const QByteArray& bytes)
{
    QFile file(name);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("Cannot write reliability fixture");
}
} // namespace
class ReliabilityTests : public QObject {
    Q_OBJECT
private slots:
    void process_kills_preserve_committed_notes_and_files_data();
    void process_kills_preserve_committed_notes_and_files();
    void autosave_survives_headless_backup_and_export_pressure();
    void reused_read_only_export_includes_other_connection_commits();
};

void ReliabilityTests::process_kills_preserve_committed_notes_and_files_data()
{
    QTest::addColumn<QString>("phase");
    for (const auto* phase : {"page-pending", "page-committed", "trash-pending", "trash-committed", "attachment-copy",
                              "attachment-promoted", "attachment-committed"})
        QTest::newRow(phase) << QString::fromLatin1(phase);
}
void ReliabilityTests::process_kills_preserve_committed_notes_and_files()
{
    QFETCH(QString, phase);
    QTemporaryDir temporary;
    const auto root = path(temporary.filePath("workspace"));
    const auto source = temporary.filePath("source.bin");
    put(source, QByteArray(262144, 'x'));
    std::string id, target, original;
    {
        auto workspace = Workspace::open(root);
        const auto journal = workspace.create_journal();
        const auto date = workspace.properties(journal.id).front().id;
        target = workspace
                     .create_page(workspace.create_database("Projects").id, "Duplicate", {},
                                  {{"heading", {}, BlockKind::Heading, {{"Heading"}}}})
                     .document.id;
        auto page = workspace.create_page(
            journal.id, "Duplicate", {{date, Date{"2026-10-29"}}},
            {{"source",
              {},
              BlockKind::Paragraph,
              {{"Original text "}, {"Heading", false, false, Reference{ReferenceKind::Block, "heading"}}}}});
        id = page.document.id;
        original = encode_document(page.document);
    }
    QProcess process;
    process.start(worker(), {phase, QString::fromStdString(root.u8string()), QString::fromStdString(id), source,
                             temporary.filePath("output")});
    QVERIFY(process.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine() || process.state() == QProcess::NotRunning, 10000);
    QCOMPARE(process.readLine(), QByteArray("ready\n"));
    process.kill();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::CrashExit);
    QVERIFY(QFile::remove(source));
    auto workspace = Workspace::open(root);
    auto page = workspace.load_page(id);
    validate(page.document);
    const bool committed = phase.endsWith("committed");
    QCOMPARE(page.trashed, phase == "trash-committed");
    QVERIFY(!page.properties.empty());
    if (!committed || phase == "trash-committed")
        QCOMPARE(encode_document(page.document), original);
    if (phase == "page-committed")
        QCOMPARE(workspace.search("committed edits").size(), std::size_t(1));
    QCOMPARE(workspace.backlinks(ReferenceKind::Page, target).size(), page.trashed ? std::size_t(0) : std::size_t(1));
    const auto files = workspace.attachments();
    QCOMPARE(files.size(), phase == "attachment-committed" ? std::size_t(1) : std::size_t(0));
    if (!files.empty())
        QCOMPARE(nebula::files::digest(workspace.attachment_path(files[0].id)).size, std::int64_t(262144));
    for (const auto& issue : workspace.audit_attachments()) {
        QVERIFY(!issue.prevents_backup);
        if (issue.removable)
            workspace.remove_unused_attachment(issue.path);
    }
    QVERIFY(workspace.audit_attachments().empty());
    if (page.trashed)
        workspace.trash_page(id, false);
    workspace.rebuild_links();
    workspace.rebuild_search();
    QCOMPARE(workspace.backlinks(ReferenceKind::Page, target).size(), std::size_t(1));
    workspace.backup_to(path(temporary.filePath("backup")));
    auto restored = Workspace::restore(path(temporary.filePath("backup")), path(temporary.filePath("restored")));
    QCOMPARE(encode_document(restored.load_page(id).document), encode_document(workspace.load_page(id).document));
    QVERIFY(restored.audit_attachments().empty());
    interchange::export_json(restored, path(temporary.filePath("json")));
    auto imported = interchange::import_json(path(temporary.filePath("json")), path(temporary.filePath("imported")));
    QCOMPARE(encode_document(imported.load_page(id).document), encode_document(restored.load_page(id).document));
    QCOMPARE(imported.backlinks(ReferenceKind::Page, target).size(), std::size_t(1));
    QVERIFY(imported.audit_attachments().empty());
}

void ReliabilityTests::autosave_survives_headless_backup_and_export_pressure()
{
    QTemporaryDir temporary(qEnvironmentVariable("NEBULA_TEST_TMPDIR", QDir::tempPath()) +
                            "/nebula-reliability-XXXXXX");
    const auto root = path(temporary.filePath("workspace"));
    auto workspace = Workspace::open(root);
    const auto database = workspace.create_database("Notes");
    const auto page = workspace.create_page(database.id, "Edited note");
    workspace.transaction([&] {
        for (int i = 0; i < 2000; ++i)
            workspace.create_page(
                database.id, "Bulk " + std::to_string(i), {},
                {{"bulk-" + std::to_string(i), {}, BlockKind::Paragraph, {{std::string(16384, 'x')}}}});
    });
    WorkspaceWindow window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(window.open_page(page.document.id));
    auto* editor = window.page_editor();
    editor->setFocus();
    QProcess process;
    process.start(worker(), {"maintenance", QString::fromStdString(root.u8string()),
                             QString::fromStdString(page.document.id), temporary.filePath("unused"), temporary.path()});
    QVERIFY(process.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine() || process.state() == QProcess::NotRunning, 10000);
    QCOMPARE(process.readLine(), QByteArray("ready\n"));
    int edits = 0;
    QString save_failure;
    QTimer typing, monitor;
    connect(&typing, &QTimer::timeout, &window, [&] {
        QTest::keyClick(editor, Qt::Key_X);
        ++edits;
    });
    connect(&monitor, &QTimer::timeout, &window, [&] {
        const auto state = window.findChild<QLabel*>("saveState")->text();
        if (state.startsWith("Save failed"))
            save_failure = state;
    });
    typing.start(650);
    monitor.start(10);
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 60000);
    typing.stop();
    monitor.stop();
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    QVERIFY2(save_failure.isEmpty(), qPrintable(save_failure));
    QVERIFY(edits >= 3);
    const auto saved_before_flush = workspace.load_page(page.document.id).revision - page.revision;
    QVERIFY(saved_before_flush >= 3);
    QVERIFY(window.flush());
    QCOMPARE(encode_document(workspace.load_page(page.document.id).document), encode_document(editor->snapshot()));
    auto restored = Workspace::restore(path(temporary.filePath("backup")), path(temporary.filePath("restored")));
    QCOMPARE(restored.pages(database.id).size(), std::size_t(2001));
    auto imported = interchange::import_json(path(temporary.filePath("json")), path(temporary.filePath("imported")));
    QCOMPARE(imported.pages(database.id).size(), std::size_t(2001));
    qInfo().noquote() << "Concurrent maintenance:" << process.readAllStandardOutput().trimmed()
                      << "database_bytes=" << QFileInfo(temporary.filePath("workspace/nebula.sqlite3")).size()
                      << "edits=" << edits << "autosaves=" << saved_before_flush << "save_failures=0";
}
void ReliabilityTests::reused_read_only_export_includes_other_connection_commits()
{
    QTemporaryDir temporary;
    const auto root = path(temporary.filePath("workspace"));
    auto writer = Workspace::open(root);
    auto page = writer.create_page(writer.create_database("Notes").id, "Before");
    auto reader = Workspace::read_only(root);
    for (int i = 0; i < 3; ++i) {
        page.title = "After " + std::to_string(i);
        page.document.blocks.front().runs = {{"Fresh committed text " + std::to_string(i)}};
        writer.save_page(page);
        const auto folder = temporary.filePath("export-" + QString::number(i));
        interchange::export_json(reader, path(folder));
        auto imported =
            interchange::import_json(path(folder), path(temporary.filePath("import-" + QString::number(i))));
        QCOMPARE(imported.load_page(page.document.id).title, page.title);
        QCOMPARE(encode_document(imported.load_page(page.document.id).document), encode_document(page.document));
    }
    sqlite3* raw = nullptr;
    QCOMPARE(sqlite3_open((root / "nebula.sqlite3").u8string().c_str(), &raw), SQLITE_OK);
    std::unique_ptr<sqlite3, DatabaseCloser> connection(raw);
    QCOMPARE(sqlite3_exec(raw, "UPDATE page_search_content SET c4='corrupted' WHERE c4<>''", nullptr, nullptr, nullptr),
             SQLITE_OK);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                             interchange::export_json(reader, path(temporary.filePath("refuse-corrupt-index"))));
    QCOMPARE(encode_document(writer.load_page(page.document.id).document), encode_document(page.document));
}

QTEST_MAIN(ReliabilityTests)
#include "reliability_tests.moc"
