#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"

#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <unordered_set>

using namespace nebula;

namespace {
Page short_page()
{
    return {"test-page", {{"a", "", BlockKind::Paragraph, {{"alpha"}}},
                          {"b", "", BlockKind::Paragraph, {{"bravo"}}},
                          {"c", "", BlockKind::Paragraph, {{"charlie"}}}}};
}

QString text(const Block& block)
{
    QString result;
    for (const auto& run : block.runs)
        result += QString::fromStdString(run.text);
    return result;
}

QStringList ids(const Page& page)
{
    QStringList result;
    for (const auto& block : page.blocks)
        result << QString::fromStdString(block.id);
    return result;
}

void ready(PageEditor& editor, const Page& page = short_page())
{
    editor.set_page(page);
    editor.resize(700, 500);
    editor.show();
    editor.setFocus();
    QApplication::processEvents();
}

void at(PageEditor& editor, int position)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(position);
    editor.setTextCursor(cursor);
}
} // namespace

class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void load_preserves_model();
    void wrapping_does_not_create_blocks();
    void enter_splits_with_new_identity();
    void splitting_parent_keeps_children_with_trailing_block();
    void enter_continues_lists_and_resets_checkbox();
    void shift_enter_keeps_identity();
    void backspace_merges_and_undo_restores_identity();
    void delete_merges_and_undo_restores_identity();
    void paste_normalizes_line_endings_and_ids();
    void keyboard_selection_spans_blocks();
    void cut_and_undo_preserve_blocks();
    void formatting_spans_blocks_and_undoes();
    void block_kind_conversion_preserves_content_and_references();
    void move_subtree_with_keyboard_and_undo();
    void indent_and_outdent_with_keyboard();
    void deleting_parent_repairs_surviving_children();
    void references_keep_identity_and_do_not_leak();
    void reference_can_be_followed_by_mouse();
    void todo_state_undoes();
    void cards_are_selectable_and_undoable();
    void editing_around_card_survives_reorder();
    void input_method_preedit_and_commit();
    void container_font_baseline_renders_sample_scripts();
    void accessible_editor_interface();
    void comparison_selection_stays_in_one_widget();
    void comparison_undo_is_per_widget();
    void long_page_edit_remains_valid();
};

void EditorTests::load_preserves_model()
{
    PageEditor editor; ready(editor, sample_page());
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(ids(page), ids(sample_page()));
    QCOMPARE(page.blocks[3].parent_id, std::string("list"));
    QCOMPARE(text(page.blocks[6]), QString::fromUtf8(u8"日本語のメモ · café · 🌙 · العربية"));
    QCOMPARE(page.blocks[5].runs[1].reference->target_id, std::string("heading"));
    QVERIFY(page.blocks[7].runs[0].asset->image);
    QVERIFY(!page.blocks[8].runs[0].asset->image);
    QVERIFY(!editor.document()->isUndoAvailable());
}

void EditorTests::wrapping_does_not_create_blocks()
{
    PageEditor editor; ready(editor, long_page(3));
    const auto original = ids(editor.snapshot());
    editor.resize(180, 400); QApplication::processEvents();
    QVERIFY(editor.document()->begin().layout()->lineCount() > 1);
    QCOMPARE(editor.document()->blockCount(), 3);
    QCOMPARE(ids(editor.snapshot()), original);
}

void EditorTests::enter_splits_with_new_identity()
{
    PageEditor editor; ready(editor); at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(4));
    QCOMPARE(page.blocks[0].id, std::string("a"));
    QVERIFY(page.blocks[1].id != "a");
    QCOMPARE(text(page.blocks[0]), QString("al"));
    QCOMPARE(text(page.blocks[1]), QString("pha"));
    QCOMPARE(page.blocks[2].id, std::string("b"));
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.redo(); QCOMPARE(ids(editor.snapshot()), ids(page));
}

void EditorTests::splitting_parent_keeps_children_with_trailing_block()
{
    PageEditor editor; ready(editor, sample_page()); editor.go_to_block("list");
    auto cursor = editor.textCursor(); cursor.movePosition(QTextCursor::NextCharacter);
    editor.setTextCursor(cursor); QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks[4].id, std::string("child"));
    QCOMPARE(page.blocks[4].parent_id, page.blocks[3].id);
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[3].parent_id, std::string("list"));
}

void EditorTests::enter_continues_lists_and_resets_checkbox()
{
    PageEditor editor; ready(editor);
    editor.set_block_kind(BlockKind::Todo); editor.toggle_checked();
    QTest::keyClick(&editor, Qt::Key_End);
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks[0].kind, BlockKind::Todo);
    QVERIFY(page.blocks[0].checked);
    QCOMPARE(page.blocks[1].kind, BlockKind::Todo);
    QVERIFY(!page.blocks[1].checked);
    QVERIFY(editor.textCursor().currentList());
    editor.undo(); QCOMPARE(editor.snapshot().blocks.size(), std::size_t(3));
}

void EditorTests::shift_enter_keeps_identity()
{
    PageEditor editor; ready(editor); at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(ids(page), ids(short_page()));
    QVERIFY(text(page.blocks[0]).contains(QChar::LineSeparator));
    editor.undo(); QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
}

void EditorTests::backspace_merges_and_undo_restores_identity()
{
    PageEditor editor; ready(editor); editor.go_to_block("b");
    QTest::keyClick(&editor, Qt::Key_Backspace);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(ids(page), QStringList({"a", "c"}));
    QCOMPARE(text(page.blocks[0]), QString("alphabravo"));
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.redo(); QCOMPARE(ids(editor.snapshot()), ids(page));
}

void EditorTests::delete_merges_and_undo_restores_identity()
{
    PageEditor editor; ready(editor); at(editor, 5);
    QTest::keyClick(&editor, Qt::Key_Delete);
    validate(editor.snapshot());
    QCOMPARE(ids(editor.snapshot()), QStringList({"a", "c"}));
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(short_page()));
}

void EditorTests::paste_normalizes_line_endings_and_ids()
{
    PageEditor editor; ready(editor); at(editor, 5);
    QApplication::clipboard()->setText("one\r\ntwo\rthree\n");
    QTest::keyClick(&editor, Qt::Key_V, Qt::ControlModifier);
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(6));
    QCOMPARE(text(page.blocks[0]), QString("alphaone"));
    QCOMPARE(text(page.blocks[1]), QString("two"));
    QCOMPARE(text(page.blocks[2]), QString("three"));
    QVERIFY(text(page.blocks[3]).isEmpty());
    QCOMPARE(page.blocks[4].id, std::string("b"));
    editor.undo(); QCOMPARE(editor.toPlainText(), QString("alpha\nbravo\ncharlie"));
}

void EditorTests::keyboard_selection_spans_blocks()
{
    PageEditor editor; ready(editor); at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(editor.textCursor().selectedText().contains("bravo"));
    QVERIFY(editor.textCursor().selectedText().contains("charlie"));
    editor.copy();
    QCOMPARE(QApplication::clipboard()->text(), QString("pha\nbravo\ncharlie"));
}

void EditorTests::cut_and_undo_preserve_blocks()
{
    PageEditor editor; ready(editor); at(editor, 2);
    auto cursor = editor.textCursor(); cursor.setPosition(9, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_X, Qt::ControlModifier);
    validate(editor.snapshot());
    QCOMPARE(editor.toPlainText(), QString("alvo\ncharlie"));
    QCOMPARE(QApplication::clipboard()->text(), QString("pha\nbra"));
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    QCOMPARE(editor.toPlainText(), QString("alpha\nbravo\ncharlie"));
}

void EditorTests::formatting_spans_blocks_and_undoes()
{
    PageEditor editor; ready(editor);
    auto cursor = editor.textCursor(); cursor.select(QTextCursor::Document);
    editor.setTextCursor(cursor); editor.toggle_bold(); editor.toggle_italic();
    for (const auto& block : editor.snapshot().blocks) {
        QVERIFY(block.runs[0].bold); QVERIFY(block.runs[0].italic);
    }
    editor.undo();
    for (const auto& block : editor.snapshot().blocks) {
        QVERIFY(block.runs[0].bold); QVERIFY(!block.runs[0].italic);
    }
    editor.undo(); QVERIFY(!editor.snapshot().blocks[0].runs[0].bold);
}

void EditorTests::block_kind_conversion_preserves_content_and_references()
{
    PageEditor editor; ready(editor, sample_page()); editor.go_to_block("reference");
    const auto original = editor.snapshot();
    for (const auto kind : {BlockKind::Heading, BlockKind::Bullet, BlockKind::Paragraph}) {
        editor.set_block_kind(kind);
        const auto page = editor.snapshot(); validate(page);
        QCOMPARE(ids(page), ids(original));
        QCOMPARE(page.blocks[5].kind, kind);
        QCOMPARE(text(page.blocks[5]), text(original.blocks[5]));
        QCOMPARE(page.blocks[5].runs[1].reference->target_id, std::string("heading"));
        QCOMPARE(editor.textCursor().currentList() != nullptr, kind == BlockKind::Bullet);
        editor.undo();
        QCOMPARE(editor.snapshot().blocks[5].kind, BlockKind::Paragraph);
        QCOMPARE(editor.current_block_id(), QString("reference"));
    }
}

void EditorTests::move_subtree_with_keyboard_and_undo()
{
    PageEditor editor; ready(editor, sample_page()); editor.go_to_block("list");
    QTest::keyClick(&editor, Qt::Key_Down, Qt::AltModifier);
    const auto moved = editor.snapshot(); validate(moved);
    QCOMPARE(moved.blocks[2].id, std::string("todo"));
    QCOMPARE(moved.blocks[3].id, std::string("list"));
    QCOMPARE(moved.blocks[4].id, std::string("child"));
    QCOMPARE(editor.current_block_id(), QString("list"));
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    editor.redo(); QCOMPARE(ids(editor.snapshot()), ids(moved));
    QVERIFY(editor.snapshot().blocks[5].runs[1].reference.has_value());
}

void EditorTests::indent_and_outdent_with_keyboard()
{
    PageEditor editor; ready(editor); editor.go_to_block("b");
    QTest::keyClick(&editor, Qt::Key_Tab);
    auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks[1].parent_id, std::string("a"));
    QTest::keyClick(&editor, Qt::Key_Backtab, Qt::ShiftModifier);
    page = editor.snapshot(); validate(page);
    QVERIFY(page.blocks[1].parent_id.empty());
    editor.undo(); QCOMPARE(editor.snapshot().blocks[1].parent_id, std::string("a"));
}

void EditorTests::deleting_parent_repairs_surviving_children()
{
    PageEditor editor; ready(editor, sample_page());
    editor.go_to_block("list"); auto cursor = editor.textCursor();
    cursor.setPosition(cursor.block().next().position(), QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor); editor.cut();
    validate(editor.snapshot());
    editor.undo(); validate(editor.snapshot());
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
}

void EditorTests::references_keep_identity_and_do_not_leak()
{
    PageEditor editor; ready(editor); at(editor, 0);
    editor.insert_reference("heading", ReferenceKind::Block, "c");
    QTest::keyClicks(&editor, " after");
    const auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks[0].runs[0].reference->target_id, std::string("c"));
    QVERIFY(!page.blocks[0].runs.back().reference.has_value());
    editor.go_to_block("c"); QTest::keyClicks(&editor, "Renamed ");
    QCOMPARE(editor.snapshot().blocks[0].runs[0].reference->target_id, std::string("c"));
    QVERIFY(editor.go_to_block("c"));
    QVERIFY(!editor.go_to_block("missing"));
}

void EditorTests::reference_can_be_followed_by_mouse()
{
    PageEditor editor; ready(editor); editor.insert_reference("jump", ReferenceKind::Block, "b");
    at(editor, 1); const auto point = editor.cursorRect().center();
    QSignalSpy spy(&editor, &PageEditor::reference_activated);
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, point);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), static_cast<int>(ReferenceKind::Block));
    QCOMPARE(spy[0][1].toString(), QString("b"));
}

void EditorTests::todo_state_undoes()
{
    PageEditor editor; ready(editor); editor.set_block_kind(BlockKind::Todo);
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Todo);
    editor.toggle_checked(); QVERIFY(editor.snapshot().blocks[0].checked);
    editor.undo(); QVERIFY(!editor.snapshot().blocks[0].checked);
    editor.undo(); QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Paragraph);
}

void EditorTests::cards_are_selectable_and_undoable()
{
    PageEditor editor; ready(editor);
    editor.insert_card(true, "sample");
    auto page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(4));
    QVERIFY(page.blocks[1].runs[0].asset->image);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    page = editor.snapshot(); validate(page);
    QCOMPARE(page.blocks[1].kind, BlockKind::Paragraph);
    editor.undo(); QVERIFY(editor.snapshot().blocks[1].runs[0].asset.has_value());
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.insert_card(false, "notes.txt");
    QVERIFY(!editor.snapshot().blocks[1].runs[0].asset->image);
}

void EditorTests::editing_around_card_survives_reorder()
{
    PageEditor editor; ready(editor); editor.insert_card(false, "notes.txt");
    QTest::keyClicks(&editor, " suffix");
    const auto before = editor.toPlainText();
    QVERIFY(editor.move_current(1)); QVERIFY(editor.move_current(-1));
    QCOMPARE(editor.toPlainText(), before);
    validate(editor.snapshot());
}

void EditorTests::input_method_preedit_and_commit()
{
    PageEditor editor; ready(editor); at(editor, 0);
    QInputMethodEvent preedit(QString::fromUtf8(u8"に"), {});
    QApplication::sendEvent(&editor, &preedit);
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
    QInputMethodEvent commit; commit.setCommitString(QString::fromUtf8(u8"日本語🌙"));
    QApplication::sendEvent(&editor, &commit);
    QCOMPARE(text(editor.snapshot().blocks[0]), QString::fromUtf8(u8"日本語🌙alpha"));
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.undo(); QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
}

void EditorTests::container_font_baseline_renders_sample_scripts()
{
    if (qgetenv("NEBULA_REQUIRE_SAMPLE_FONTS") != "1")
        QSKIP("The multilingual font baseline is enforced in the development image.");
    PageEditor editor;
    const QFontMetrics metrics(editor.font());
    QVERIFY2(metrics.inFontUcs4(0x65e5), "Japanese sample text needs a CJK fallback font");
    QVERIFY2(metrics.inFontUcs4(0x1f319), "The sample moon needs an emoji fallback font");
    QVERIFY2(metrics.inFontUcs4(0x0639), "Arabic sample text needs a fallback font");
}

void EditorTests::accessible_editor_interface()
{
    PageEditor editor; ready(editor);
    auto* accessible = QAccessible::queryAccessibleInterface(&editor);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::EditableText);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Page block editor"));
    QVERIFY(accessible->textInterface());
}

void EditorTests::comparison_selection_stays_in_one_widget()
{
    BlockWidgets comparison; comparison.set_page(short_page());
    comparison.resize(700, 500); comparison.show(); QApplication::processEvents();
    auto* first = comparison.editors()[0]; first->setFocus();
    QTest::keyClick(first, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(first->textCursor().selectedText(), QString("alpha"));
    QVERIFY(!comparison.editors()[1]->textCursor().hasSelection());
    QVERIFY(!comparison.editors()[2]->textCursor().hasSelection());
}

void EditorTests::comparison_undo_is_per_widget()
{
    BlockWidgets comparison; comparison.set_page(short_page());
    auto* first = comparison.editors()[0]; auto* second = comparison.editors()[1];
    first->insertPlainText("1"); second->insertPlainText("2");
    second->undo();
    QCOMPARE(first->toPlainText(), QString("1alpha"));
    QCOMPARE(second->toPlainText(), QString("bravo"));
    first->undo(); QCOMPARE(first->toPlainText(), QString("alpha"));
}

void EditorTests::long_page_edit_remains_valid()
{
    PageEditor editor; ready(editor, long_page(1000));
    editor.go_to_block("block-500"); QTest::keyClick(&editor, Qt::Key_Return);
    validate(editor.snapshot()); QCOMPARE(editor.document()->blockCount(), 1001);
    editor.undo(); QCOMPARE(ids(editor.snapshot()), ids(long_page(1000)));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
