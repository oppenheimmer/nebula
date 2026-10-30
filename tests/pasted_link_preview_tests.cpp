#include "nebula/app/pasted_link_preview.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QInputMethodEvent>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <functional>
#include <sqlite3.h>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& value)
{
    return std::filesystem::u8path(value.toUtf8().constData());
}
class Web final : public QObject {
public:
    QTcpServer server;
    QList<QPointer<QTcpSocket>> peers;
    QList<QByteArray> requests;
    Web()
    {
        if (!server.listen(QHostAddress::LocalHost))
            qFatal("Cannot listen on loopback");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                peers.append(socket);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket, bytes = QByteArray{}, handled = false]() mutable {
                            bytes += socket->readAll();
                            if (handled || !bytes.contains("\r\n\r\n"))
                                return;
                            handled = true;
                            requests.append(bytes);
                        });
            }
        });
    }
    QString url(const QString& suffix = {}) const
    {
        return QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(suffix);
    }
    void reply(const QByteArray& body = "<title>Extracted title</title><p>Text 日本語</p>", int status = 200)
    {
        for (auto socket : peers)
            if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                socket->write("HTTP/1.1 " + QByteArray::number(status) +
                              " Response\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            }
    }
};
void show(QMainWindow& window, PageEditor* editor)
{
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    editor->setFocus();
    QApplication::processEvents();
}
struct EditorWindow : QMainWindow {
    PageEditor* editor = new PageEditor(this);
    EditorWindow()
    {
        setCentralWidget(editor);
        editor->set_page(
            {"page", {{"source", {}, BlockKind::Paragraph, {}}, {"tail", {}, BlockKind::Paragraph, {{"Keep tail"}}}}});
    }
};
void paste(PageEditor* editor, PastedLinkPreview& controller, const QString& url)
{
    QApplication::clipboard()->setText(url);
    QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
    QVERIFY(controller.running());
}
void sql(const QString& file, const char* command)
{
    sqlite3* raw = nullptr;
    sqlite3_open(file.toUtf8().constData(), &raw);
    std::unique_ptr<sqlite3, DatabaseCloser> database(raw);
    if (sqlite3_exec(raw, command, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("Test SQL setup failed");
}
QString block_text(const Block& block)
{
    QString text;
    for (const auto& run : block.runs)
        text += QString::fromUtf8(run.text.data(), qsizetype(run.text.size()));
    return text;
}
void restore(WorkspaceWindow& window, const QString& id)
{
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QTimer::singleShot(1500, dialog, &QDialog::reject);
        auto* list = dialog->findChild<QListWidget*>("trashItems");
        QVERIFY(list);
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toString() == id) {
                list->setCurrentRow(row);
                dialog->findChild<QPushButton*>("restoreSelected")->click();
                dialog->accept();
                return;
            }
        QFAIL("Missing recovery group");
    });
    window.findChild<QAction*>("restoreTrash")->trigger();
}
} // namespace
class PastedLinkPreviewTests : public QObject {
    Q_OBJECT
private slots:
    void insertion_preserves_tree_position_selection_and_undo();
    void cancellation_and_timeout_keep_the_link();
    void stale_results_data();
    void stale_results();
    void newer_paste_supersedes_the_old_fetch();
    void composition_data();
    void composition();
    void persistence_clipboard_trash_restore_and_save_failure();
    void controller_destruction_removes_pending_controls();
    void editor_destruction_cancels_pending_fetch();
    void modal_operations_defer_insertion();
    void external_html_url_pastes_plain_text_and_fetches_once();
    void ordinary_text_and_internal_references_do_not_fetch();
    void plain_and_canonical_url_paste_use_the_same_workflow();
    void read_only_editor_does_not_paste_or_fetch();
};
void PastedLinkPreviewTests::insertion_preserves_tree_position_selection_and_undo()
{
    Web web;
    EditorWindow window;
    window.editor->set_page({"page",
                             {{"source", {}, BlockKind::Heading, {{"Read "}}},
                              {"child", "source", BlockKind::Bullet, {{"Child"}}},
                              {"tail", {}, BlockKind::Paragraph, {{"Keep tail"}}}}});
    show(window, window.editor);
    QTest::keyClick(window.editor, Qt::Key_End);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    paste(window.editor, controller, web.url());
    QTRY_COMPARE(web.requests.size(), 1);
    const auto pasted = encode_document(window.editor->snapshot());
    QVERIFY(window.editor->go_to_block("tail"));
    auto cursor = window.editor->textCursor();
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 4);
    window.editor->setTextCursor(cursor);
    const auto position = window.editor->capture_position();
    web.reply();
    QTRY_COMPARE(inserted.size(), 1);
    const auto page = window.editor->snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(4));
    QCOMPARE(page.blocks[0].id, std::string("source"));
    QCOMPARE(page.blocks[1].id, std::string("child"));
    QCOMPARE(page.blocks[3].id, std::string("tail"));
    const auto& preview = page.blocks[2];
    QCOMPARE(preview.kind, BlockKind::Paragraph);
    QVERIFY(preview.parent_id.empty());
    QVERIFY(preview.attachment_id.empty());
    QVERIFY(block_text(preview).contains("Extracted title"));
    QVERIFY(block_text(preview).contains(QString::fromUtf8("日本語")));
    for (const auto& run : preview.runs) {
        QVERIFY(!run.asset);
        QVERIFY(!run.reference);
    }
    QVERIFY(preview.runs[0].bold);
    const auto restored = window.editor->capture_position();
    QCOMPARE(restored.block, position.block);
    QCOMPARE(restored.anchor_block, position.anchor_block);
    QCOMPARE(restored.offset, position.offset);
    QCOMPARE(restored.anchor_offset, position.anchor_offset);
    QCOMPARE(restored.scroll, position.scroll);
    window.editor->undo();
    QCOMPARE(encode_document(window.editor->snapshot()), pasted);
    window.editor->redo();
    QCOMPARE(encode_document(window.editor->snapshot()), encode_document(page));
    window.editor->undo();
    window.editor->undo();
    QCOMPARE(block_text(window.editor->snapshot().blocks[0]), QString("Read "));
    window.editor->redo();
    window.editor->redo();
    QCOMPARE(encode_document(window.editor->snapshot()), encode_document(page));
    QCOMPARE(web.requests.size(), 1);
}
void PastedLinkPreviewTests::cancellation_and_timeout_keep_the_link()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar(), nullptr, {100, 1024, 3});
    QSignalSpy done(&controller, &PastedLinkPreview::preview_finished);
    paste(window.editor, controller, web.url());
    const auto original = encode_document(window.editor->snapshot());
    auto* cancel = window.findChild<QToolButton*>("cancelLinkPreview");
    QVERIFY(cancel);
    cancel->setFocus();
    QTest::keyClick(cancel, Qt::Key_Space);
    QVERIFY(!controller.running());
    QCOMPARE(done.size(), 1);
    QVERIFY(done[0][0].toString().contains("cancelled"));
    web.reply();
    QTest::qWait(30);
    QCOMPARE(encode_document(window.editor->snapshot()), original);
    window.editor->setFocus();
    controller.begin(web.url(), "source");
    QTRY_COMPARE(done.size(), 2);
    QVERIFY(done[1][0].toString().contains("timed out"));
    QCOMPARE(encode_document(window.editor->snapshot()), original);
    PastedLinkPreview failure(window.editor, window.statusBar());
    QSignalSpy failed(&failure, &PastedLinkPreview::preview_finished);
    const auto requests = web.requests.size();
    failure.begin(web.url(), "source");
    QTRY_COMPARE(web.requests.size(), requests + 1);
    web.reply("<p>Not found</p>", 404);
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed[0][0].toString().contains("could not be retrieved"));
    QCOMPARE(encode_document(window.editor->snapshot()), original);
}
void PastedLinkPreviewTests::stale_results_data()
{
    QTest::addColumn<QString>("change");
    for (const auto* name : {"undo", "extend-url", "navigate", "remove", "hide"})
        QTest::newRow(name) << QString::fromLatin1(name);
}
void PastedLinkPreviewTests::stale_results()
{
    QFETCH(QString, change);
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    QSignalSpy done(&controller, &PastedLinkPreview::preview_finished);
    paste(window.editor, controller, web.url());
    QTRY_COMPARE(web.requests.size(), 1);
    if (change == "undo")
        window.editor->undo();
    else if (change == "extend-url")
        QTest::keyClicks(window.editor, "/changed");
    else if (change == "navigate")
        window.editor->set_page({"other-page", {{"other", {}, BlockKind::Paragraph, {{"Other note"}}}}});
    else if (change == "remove")
        window.editor->apply_document({"page", {{"tail", {}, BlockKind::Paragraph, {{"Keep tail"}}}}}, "tail");
    else
        window.hide();
    const auto changed = encode_document(window.editor->snapshot());
    QTRY_VERIFY_WITH_TIMEOUT(!controller.running(), 1000);
    QCOMPARE(done.size(), 1);
    QVERIFY(done[0][0].toString().contains("cancelled"));
    web.reply();
    QTest::qWait(30);
    QCOMPARE(inserted.size(), 0);
    QCOMPARE(encode_document(window.editor->snapshot()), changed);
}
void PastedLinkPreviewTests::newer_paste_supersedes_the_old_fetch()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    paste(window.editor, controller, web.url("old"));
    QTRY_COMPARE(web.requests.size(), 1);
    window.editor->selectAll();
    paste(window.editor, controller, web.url("new"));
    QTRY_COMPARE(web.requests.size(), 2);
    web.reply("<title>New preview</title><p>Current link</p>");
    QTRY_COMPARE(inserted.size(), 1);
    const auto page = window.editor->snapshot();
    QCOMPARE(page.blocks.size(), std::size_t(2));
    QCOMPARE(block_text(page.blocks[0]), web.url("new"));
    QVERIFY(block_text(page.blocks[1]).contains("New preview"));
}
void PastedLinkPreviewTests::composition_data()
{
    QTest::addColumn<bool>("commit");
    QTest::newRow("commit") << true;
    QTest::newRow("cancel") << false;
}
void PastedLinkPreviewTests::composition()
{
    QFETCH(bool, commit);
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    paste(window.editor, controller, web.url());
    QTRY_COMPARE(web.requests.size(), 1);
    QVERIFY(window.editor->go_to_block("tail"));
    QInputMethodEvent preedit(QString::fromUtf8("あ"), {});
    QApplication::sendEvent(window.editor, &preedit);
    web.reply();
    QTest::qWait(50);
    QCOMPARE(inserted.size(), 0);
    QVERIFY(controller.running());
    QInputMethodEvent end;
    if (commit)
        end.setCommitString(QString::fromUtf8("あ"));
    QApplication::sendEvent(window.editor, &end);
    QTRY_COMPARE(inserted.size(), 1);
    const auto page = window.editor->snapshot();
    QVERIFY(block_text(page.blocks.back()).contains(QString::fromUtf8("あ")) == commit);
    window.editor->undo();
    QCOMPARE(window.editor->snapshot().blocks.size(), std::size_t(2));
    QVERIFY(block_text(window.editor->snapshot().blocks.back()).contains(QString::fromUtf8("あ")) == commit);
}
void PastedLinkPreviewTests::persistence_clipboard_trash_restore_and_save_failure()
{
    Web web;
    QTemporaryDir folder;
    std::string page_id, recovery;
    QString preview_id;
    std::string full_document;
    {
        auto workspace = Workspace::open(path(folder.path()));
        const auto db = workspace.create_database("Notes");
        const auto page = workspace.create_page(db.id, "Link");
        page_id = page.document.id;
        WorkspaceWindow window(workspace);
        show(window, window.page_editor());
        QVERIFY(window.open_page(page_id));
        auto* controller = window.findChild<PastedLinkPreview*>("pastedLinkPreview");
        QVERIFY(controller);
        QSignalSpy inserted(controller, &PastedLinkPreview::preview_inserted);
        paste(window.page_editor(), *controller, web.url());
        QVERIFY(window.flush());
        QTRY_COMPARE(web.requests.size(), 1);
        sql(folder.filePath("nebula.sqlite3"), "DROP TABLE page_search");
        web.reply();
        QTRY_COMPARE(inserted.size(), 1);
        preview_id = inserted[0][0].toString();
        QVERIFY(!window.flush());
        QVERIFY(window.findChild<QLabel*>("saveState")->text().startsWith("Save failed"));
        QCOMPARE(workspace.load_page(page_id).document.blocks.size(), std::size_t(1));
        QCOMPARE(block_text(workspace.load_page(page_id).document.blocks[0]), web.url());
        workspace.rebuild_search();
        QVERIFY(window.flush());
        full_document = encode_document(workspace.load_page(page_id).document);
        QTemporaryDir portable;
        workspace.backup_to(path(portable.filePath("backup")));
        auto backed_up = Workspace::restore(path(portable.filePath("backup")), path(portable.filePath("restored")));
        QCOMPARE(encode_document(backed_up.load_page(page_id).document), full_document);
        interchange::export_json(workspace, path(portable.filePath("json")));
        auto imported = interchange::import_json(path(portable.filePath("json")), path(portable.filePath("imported")));
        QCOMPARE(encode_document(imported.load_page(page_id).document), full_document);
        QVERIFY(window.page_editor()->go_to_block(preview_id));
        auto selected = window.page_editor()->textCursor();
        selected.movePosition(QTextCursor::StartOfBlock);
        selected.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        window.page_editor()->setTextCursor(selected);
        QTest::keyClick(window.page_editor(), Qt::Key_C, Qt::ControlModifier);
        window.create_page("Copy preview");
        QTest::keyClick(window.page_editor(), Qt::Key_V, Qt::ControlModifier);
        QVERIFY(window.flush());
        const auto copied = window.page_editor()->snapshot();
        QCOMPARE(copied.blocks.size(), std::size_t(1));
        QVERIFY(QString::fromStdString(copied.blocks[0].id) != preview_id);
        QVERIFY(block_text(copied.blocks[0]).contains("Extracted title"));
        QVERIFY(copied.blocks[0].runs[0].bold);
        QVERIFY(window.open_page(page_id));
        QVERIFY(window.page_editor()->go_to_block(preview_id));
        QTimer::singleShot(0, &window, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QPushButton*>("confirmTrash")->click();
        });
        QTest::mouseClick(window.findChild<QToolButton*>("trashBlockButton"), Qt::LeftButton);
        QCOMPARE(workspace.load_page(page_id).document.blocks.size(), std::size_t(1));
        QCOMPARE(block_text(workspace.load_page(page_id).document.blocks[0]), web.url());
        window.page_editor()->undo();
        QVERIFY(window.flush());
        QCOMPARE(encode_document(workspace.load_page(page_id).document), full_document);
        window.page_editor()->redo();
        QVERIFY(window.flush());
        recovery = workspace.deleted_blocks()[0].id;
        QVERIFY(workspace.attachments().empty());
        QCOMPARE(web.requests.size(), 1);
        QVERIFY(window.close());
    }
    {
        auto workspace = Workspace::open(path(folder.path()));
        WorkspaceWindow window(workspace);
        show(window, window.page_editor());
        auto* controller = window.findChild<PastedLinkPreview*>("pastedLinkPreview");
        QVERIFY(controller);
        QVERIFY(window.open_page(page_id));
        QCOMPARE(block_text(workspace.load_page(page_id).document.blocks[0]), web.url());
        restore(window, QString::fromStdString(recovery));
        QVERIFY(window.open_page(page_id));
        QCOMPARE(encode_document(window.page_editor()->snapshot()), full_document);
        QVERIFY(workspace.attachments().empty());
        QTest::qWait(30);
        QCOMPARE(web.requests.size(), 1);
        QVERIFY(!controller->running());
    }
}
void PastedLinkPreviewTests::controller_destruction_removes_pending_controls()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    auto* controller = new PastedLinkPreview(window.editor, window.statusBar());
    paste(window.editor, *controller, web.url());
    QPointer<QWidget> controls = window.findChild<QWidget*>("linkPreviewProgress");
    QVERIFY(controls);
    delete controller;
    QTRY_VERIFY(controls.isNull());
    web.reply();
    QTest::qWait(30);
    QCOMPARE(window.editor->snapshot().blocks.size(), std::size_t(2));
}
void PastedLinkPreviewTests::editor_destruction_cancels_pending_fetch()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    paste(window.editor, controller, web.url());
    QTRY_COMPARE(web.requests.size(), 1);
    delete window.takeCentralWidget();
    QVERIFY(!controller.running());
    web.reply();
    QTest::qWait(30);
    QCOMPARE(inserted.size(), 0);
}
void PastedLinkPreviewTests::modal_operations_defer_insertion()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    paste(window.editor, controller, web.url());
    QTRY_COMPARE(web.requests.size(), 1);
    QDialog dialog(&window);
    dialog.setModal(true);
    dialog.show();
    QTRY_COMPARE(QApplication::activeModalWidget(), &dialog);
    web.reply();
    QTest::qWait(150);
    QCOMPARE(inserted.size(), 0);
    QVERIFY(controller.running());
    dialog.accept();
    QTRY_COMPARE(inserted.size(), 1);
}
void PastedLinkPreviewTests::external_html_url_pastes_plain_text_and_fetches_once()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    auto* mime = new QMimeData;
    mime->setText(web.url());
    mime->setHtml("<b>" + web.url() + "</b><img src='https://example.com/thumbnail'>");
    QApplication::clipboard()->setMimeData(mime);
    QTest::keyClick(window.editor, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(web.requests.size(), 1);
    QCOMPARE(block_text(window.editor->snapshot().blocks[0]), web.url());
    web.reply();
    QTRY_COMPARE(inserted.size(), 1);
    for (const auto& block : window.editor->snapshot().blocks) {
        QVERIFY(block.attachment_id.empty());
        for (const auto& run : block.runs)
            QVERIFY(!run.asset);
    }
    QCOMPARE(web.requests.size(), 1);
}
void PastedLinkPreviewTests::ordinary_text_and_internal_references_do_not_fetch()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QApplication::clipboard()->setText("Read " + web.url());
    QTest::keyClick(window.editor, Qt::Key_V, Qt::ControlModifier);
    QTest::qWait(30);
    QVERIFY(!controller.running());
    QVERIFY(web.requests.isEmpty());
    window.editor->selectAll();
    Page fragment{"clipboard",
                  {{"reference",
                    {},
                    BlockKind::Paragraph,
                    {{web.url().toStdString(), false, false, Reference{ReferenceKind::Page, "local-target"}}}}}};
    auto* mime = new QMimeData;
    mime->setText(web.url());
    mime->setData("application/x-nebula-blocks+json", QByteArray::fromStdString(encode_document(fragment)));
    QApplication::clipboard()->setMimeData(mime);
    QTest::keyClick(window.editor, Qt::Key_V, Qt::ControlModifier);
    QTest::qWait(30);
    QVERIFY(!controller.running());
    QVERIFY(web.requests.isEmpty());
    QVERIFY(window.editor->snapshot().blocks[0].runs[0].reference);
}
void PastedLinkPreviewTests::plain_and_canonical_url_paste_use_the_same_workflow()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    QSignalSpy inserted(&controller, &PastedLinkPreview::preview_inserted);
    QApplication::clipboard()->setText(web.url());
    QTest::keyClick(window.editor, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(web.requests.size(), 1);
    web.reply();
    QTRY_COMPARE(inserted.size(), 1);
    window.editor->set_page({"next-page", {{"next-block", {}, BlockKind::Paragraph, {}}}});
    Page fragment{"clipboard", {{"link", {}, BlockKind::Paragraph, {{web.url("canonical").toStdString()}}}}};
    auto* mime = new QMimeData;
    mime->setText(web.url("canonical"));
    mime->setData("application/x-nebula-blocks+json", QByteArray::fromStdString(encode_document(fragment)));
    QApplication::clipboard()->setMimeData(mime);
    QTest::keyClick(window.editor, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(web.requests.size(), 2);
    web.reply();
    QTRY_COMPARE(inserted.size(), 2);
    QCOMPARE(block_text(window.editor->snapshot().blocks[0]), web.url("canonical"));
}
void PastedLinkPreviewTests::read_only_editor_does_not_paste_or_fetch()
{
    Web web;
    EditorWindow window;
    show(window, window.editor);
    PastedLinkPreview controller(window.editor, window.statusBar());
    const auto original = encode_document(window.editor->snapshot());
    window.editor->setReadOnly(true);
    QApplication::clipboard()->setText(web.url());
    window.editor->paste();
    QCOMPARE(encode_document(window.editor->snapshot()), original);
    window.editor->paste_plain_text();
    QCOMPARE(encode_document(window.editor->snapshot()), original);
    QVERIFY(!window.editor->document()->isUndoAvailable());
    QTest::qWait(30);
    QVERIFY(!controller.running());
    QVERIFY(web.requests.isEmpty());
}
QTEST_MAIN(PastedLinkPreviewTests)
#include "pasted_link_preview_tests.moc"
