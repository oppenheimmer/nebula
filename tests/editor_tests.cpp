#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/format/codec.hpp"

#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <unordered_set>

using namespace nebula;

namespace {
Page short_page()
{
    return {"test-page",
            {{"a", "", BlockKind::Paragraph, {{"alpha"}}},
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
    void structural_commands_keep_caret_offset_and_selection();
    void structural_commands_rewrite_only_the_affected_range();
    void plain_text_paste_drops_rich_formats();
    void merged_blocks_are_reported_as_absorbed();
    void block_state_matches_core_operations();
    void ordinary_typing_reuses_cached_external_labels();
    void load_preserves_model();
    void wrapping_does_not_create_blocks();
    void enter_splits_with_new_identity();
    void splitting_parent_keeps_children_with_trailing_block();
    void enter_continues_lists_and_resets_checkbox();
    void shift_enter_keeps_identity();
    void backspace_merges_and_undo_restores_identity();
    void backspace_at_nested_block_start_outdents_subtree();
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
    void grip_drag_reorders_and_undoes_a_subtree();
    void grip_drag_can_nest_and_outdent();
    void grip_drag_escape_and_invalid_target_do_not_edit();
    void grip_drag_scrolls_long_documents();
    void unresolved_reference_keeps_scope_and_emits_intent();
    void divider_insertion_preserves_subtree_and_undo();
    void divider_editing_becomes_a_paragraph();
    void typing_undo_groups_stop_at_navigation_and_structure();
    void rich_clipboard_preserves_tree_and_remaps_internal_references();
    void rich_clipboard_clips_unicode_and_unresolved_runs();
    void html_clipboard_keeps_supported_formatting_without_resources();
    void malformed_clipboard_leaves_document_unchanged();
    void rich_paste_preserves_suffix_and_existing_children();
    void empty_list_enter_exits_without_an_extra_block();
    void pasted_divider_does_not_consume_surrounding_text();
    void modifier_keys_do_not_change_document_or_undo_history();
    void wiki_typing_selects_only_the_token_and_conversion_undoes();
    void wiki_composition_and_escaped_opening();
    void positions_restore_selection_scroll_and_clamp_missing_content();
    void keyboard_reference_activation_preserves_document();
    void default_labels_follow_heading_edits_in_the_same_undo_group();
    void title_and_default_page_labels_undo_together();
    void editing_a_default_label_makes_it_custom_and_undo_restores_mode();
    void resolving_custom_reference_preserves_rich_label_and_undo();
    void managed_card_identity_survives_clipboard_reorder_delete_and_undo();
};

void EditorTests::modifier_keys_do_not_change_document_or_undo_history()
{
    PageEditor editor;
    ready(editor);
    QSignalSpy changes(&editor, &QTextEdit::textChanged);
    const auto before = editor.snapshot();
    QTest::keyPress(&editor, Qt::Key_Control);
    QTest::keyPress(&editor, Qt::Key_Alt);
    QTest::keyRelease(&editor, Qt::Key_Alt);
    QTest::keyRelease(&editor, Qt::Key_Control);
    QTest::keyClick(&editor, Qt::Key_Right);
    QTest::keyClick(&editor, Qt::Key_End);
    QTest::keyClick(&editor, Qt::Key_Left, Qt::ShiftModifier);
    QTest::keyClick(&editor, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(&editor, Qt::Key_Home, Qt::ControlModifier);
    // QTest's ASCII key mapper cannot generate these native IME mode keys.
    for (const auto key : {Qt::Key_Zenkaku_Hankaku, Qt::Key_Hangul, Qt::Key_Escape, Qt::Key_F1}) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(&editor, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(&editor, &release);
    }
    QCOMPARE(changes.count(), 0);
    QVERIFY(!editor.document()->isUndoAvailable());
    QCOMPARE(ids(editor.snapshot()), ids(before));
}
void EditorTests::wiki_typing_selects_only_the_token_and_conversion_undoes()
{
    PageEditor editor;
    ready(editor);
    at(editor, 0);
    QSignalSpy queries(&editor, &PageEditor::wiki_query_changed);
    connect(&editor, &PageEditor::wiki_link_requested, &editor, [&](const QString& syntax) {
        QCOMPARE(syntax, QString("[[Target|label]]"));
        QCOMPARE(editor.textCursor().selectedText(), syntax);
        editor.insert_reference("label", ReferenceKind::Page, "target-id");
    });
    QTest::keyClicks(&editor, "Before [[Target|label]]");
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("Before labelalpha"));
    QCOMPARE(editor.snapshot().blocks[0].runs[1].reference->target_id, std::string("target-id"));
    QVERIFY(!queries.empty());
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("Before [[Target|label]]alpha"));
    editor.redo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("Before labelalpha"));
    QTest::keyClicks(&editor, " after");
    QVERIFY(!editor.snapshot().blocks[0].runs.back().reference);
}
void EditorTests::wiki_composition_and_escaped_opening()
{
    PageEditor editor;
    ready(editor);
    at(editor, 0);
    QSignalSpy requests(&editor, &PageEditor::wiki_link_requested);
    QTest::keyClicks(&editor, R"(\[[literal]])");
    QVERIFY(requests.empty());
    QVERIFY(!editor.textCursor().hasSelection());
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8(u8" [[日本語]]"));
    QApplication::sendEvent(&editor, &commit);
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0][0].toString(), QString::fromUtf8(u8"[[日本語]]"));
    QCOMPARE(editor.textCursor().selectedText(), QString::fromUtf8(u8"[[日本語]]"));
    QCOMPARE(editor.snapshot().blocks.size(), std::size_t(3));
}
void EditorTests::positions_restore_selection_scroll_and_clamp_missing_content()
{
    PageEditor editor;
    auto page = long_page(100);
    ready(editor, page);
    editor.go_to_block("block-49");
    auto cursor = editor.textCursor();
    cursor.setPosition(cursor.position() + 3);
    const auto anchor = cursor.position();
    editor.go_to_block("block-50");
    const auto end = editor.textCursor().position() + 8;
    cursor.setPosition(anchor);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.verticalScrollBar()->setValue(1234);
    const auto original = editor.capture_position();
    editor.go_to_block("block-1");
    QVERIFY(editor.restore_position(original));
    QCOMPARE(editor.textCursor().anchor(), anchor);
    QCOMPARE(editor.textCursor().position(), end);
    QCOMPARE(editor.verticalScrollBar()->value(), original.scroll);
    page.blocks[50].runs = {{"x"}};
    editor.set_page(page);
    QVERIFY(editor.restore_position(original));
    QCOMPARE(editor.textCursor().positionInBlock(), 1);
    QCOMPARE(editor.capture_position().anchor_offset, 3);
    page.blocks.erase(page.blocks.begin() + 50);
    editor.set_page(page);
    QVERIFY(!editor.restore_position(original));
    QCOMPARE(editor.current_block_id(), QString("block-0"));
}
void EditorTests::keyboard_reference_activation_preserves_document()
{
    PageEditor editor;
    ready(editor,
          {"page",
           {{"ref", "", BlockKind::Paragraph, {{"Jump", false, false, Reference{ReferenceKind::Block, "target"}}}},
            {"missing",
             "",
             BlockKind::Paragraph,
             {{"Future", false, false, Reference{ReferenceKind::Page, "", "Future", "db"}}}}}});
    QSignalSpy resolved(&editor, &PageEditor::reference_activated),
        unresolved(&editor, &PageEditor::unresolved_reference_activated);
    QSignalSpy changes(&editor, &QTextEdit::textChanged);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved[0][1].toString(), QString("target"));
    editor.go_to_block("missing");
    QTest::keyClick(&editor, Qt::Key_Enter, Qt::ControlModifier);
    QCOMPARE(unresolved.size(), 1);
    QCOMPARE(unresolved[0][0].toString(), QString("Future"));
    QCOMPARE(changes.size(), 0);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.go_to_block("ref");
    QTest::keyClick(&editor, Qt::Key_End);
    editor.insert_reference("New target", ReferenceKind::Block, "new-target");
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(resolved.size(), 2);
    QCOMPARE(resolved.back()[1].toString(), QString("new-target"));
}
void EditorTests::default_labels_follow_heading_edits_in_the_same_undo_group()
{
    PageEditor editor;
    ready(
        editor,
        {"page",
         {{"heading", "", BlockKind::Heading, {{"Heading"}}},
          {"default",
           "",
           BlockKind::Paragraph,
           {{"Cached", true, false, Reference{ReferenceKind::Block, "heading", "", "", false}}}},
          {"custom", "", BlockKind::Paragraph, {{"Fixed", false, true, Reference{ReferenceKind::Block, "heading"}}}}}});
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("Heading"));
    editor.go_to_block("heading");
    QTest::keyClick(&editor, Qt::Key_End);
    QTest::keyClicks(&editor, " changed");
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("Heading changed"));
    QCOMPARE(text(editor.snapshot().blocks[2]), QString("Fixed"));
    QVERIFY(!editor.snapshot().blocks[1].runs[0].reference->custom_label);
    QVERIFY(editor.snapshot().blocks[1].runs[0].bold);
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("Heading"));
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("Heading"));
    editor.redo();
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("Heading changed"));
    QVERIFY(editor.move_current(1));
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("Heading changed"));
}
void EditorTests::title_and_default_page_labels_undo_together()
{
    PageEditor editor;
    ready(editor);
    editor.set_page({"page",
                     {{"ref",
                       "",
                       BlockKind::Paragraph,
                       {{"Cached", false, false, Reference{ReferenceKind::Page, "page", "", "", false}},
                        {" fixed", false, true, Reference{ReferenceKind::Page, "page"}}}}}},
                    "Original");
    QCOMPARE(editor.page_title(), QString("Original"));
    QCOMPARE(editor.snapshot().blocks[0].runs[0].text, std::string("Original"));
    editor.set_page_title("Renamed");
    QCOMPARE(editor.snapshot().blocks[0].runs[0].text, std::string("Renamed"));
    QCOMPARE(editor.page_title(), QString("Renamed"));
    editor.undo();
    QCOMPARE(editor.page_title(), QString("Original"));
    QCOMPARE(editor.snapshot().blocks[0].runs[0].text, std::string("Original"));
    editor.redo();
    QCOMPARE(editor.page_title(), QString("Renamed"));
    QCOMPARE(editor.snapshot().blocks[0].runs[1].text, std::string(" fixed"));
}
void EditorTests::editing_a_default_label_makes_it_custom_and_undo_restores_mode()
{
    PageEditor editor;
    ready(editor, {"page",
                   {{"heading", "", BlockKind::Heading, {{"Heading"}}},
                    {"ref",
                     "",
                     BlockKind::Paragraph,
                     {{"Cached", false, false, Reference{ReferenceKind::Block, "heading", "", "", false}}}}}});
    editor.go_to_block("ref");
    QTest::keyClick(&editor, Qt::Key_Right);
    QTest::keyClicks(&editor, "custom");
    auto run = editor.snapshot().blocks[1].runs[0];
    QVERIFY(run.reference->custom_label);
    QVERIFY(run.text.find("custom") != std::string::npos);
    editor.undo();
    run = editor.snapshot().blocks[1].runs[0];
    QVERIFY(!run.reference->custom_label);
    QCOMPARE(run.text, std::string("Heading"));
    editor.redo();
    QVERIFY(editor.snapshot().blocks[1].runs[0].reference->custom_label);
}
void EditorTests::resolving_custom_reference_preserves_rich_label_and_undo()
{
    const Reference unresolved{ReferenceKind::Page, "", "Future", "db", true};
    PageEditor editor;
    ready(editor, {"page",
                   {{"ref",
                     "",
                     BlockKind::Paragraph,
                     {{"read", true, false, unresolved}, {" notes", false, true, unresolved}}}}});
    QVERIFY(editor.select_current_reference());
    const auto location = editor.selected_reference_location();
    QVERIFY(location);
    QCOMPARE(location->first_run, std::size_t(0));
    QCOMPARE(location->run_count, std::size_t(2));
    editor.replace_selected_reference({ReferenceKind::Page, "target"}, "read notes");
    auto block = editor.snapshot().blocks[0];
    QCOMPARE(block.runs.size(), std::size_t(2));
    QVERIFY(block.runs[0].bold);
    QVERIFY(block.runs[1].italic);
    QCOMPARE(block.runs[1].reference->target_id, std::string("target"));
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[0].runs[1].reference->unresolved_name, std::string("Future"));
    editor.redo();
    QCOMPARE(editor.snapshot().blocks[0].runs[0].reference->target_id, std::string("target"));
}
void EditorTests::managed_card_identity_survives_clipboard_reorder_delete_and_undo()
{
    const Page original{"page",
                        {{"image", "", BlockKind::Image, {}, "image.png", false, "owned-file"},
                         {"following", "", BlockKind::Paragraph, {{"Text"}}}}};
    PageEditor editor;
    ready(editor, original);
    QCOMPARE(editor.snapshot().blocks[0].attachment_id, std::string("owned-file"));
    QCOMPARE(editor.snapshot().blocks[0].runs[0].asset->attachment_id, std::string("owned-file"));
    auto selected = editor.textCursor();
    selected.select(QTextCursor::Document);
    editor.setTextCursor(selected);
    editor.copy();
    PageEditor target;
    ready(target, {"target", {{"empty", "", BlockKind::Paragraph, {}}}});
    target.paste();
    QCOMPARE(target.snapshot().blocks[0].attachment_id, std::string("owned-file"));
    QCOMPARE(target.snapshot().blocks[0].runs[0].asset->attachment_id, std::string("owned-file"));
    editor.go_to_block("image");
    QVERIFY(editor.move_current(1));
    QCOMPARE(editor.snapshot().blocks[1].attachment_id, std::string("owned-file"));
    editor.undo();
    editor.go_to_block("image");
    QTest::keyClick(&editor, Qt::Key_Delete);
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Paragraph);
    QVERIFY(editor.snapshot().blocks[0].attachment_id.empty());
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[0].attachment_id, std::string("owned-file"));
}
void EditorTests::grip_drag_reorders_and_undoes_a_subtree()
{
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("todo");
    const auto target = QPoint(18, qRound(editor.active_block_rect().bottom() - 2));
    editor.go_to_block("list");
    const auto grip = editor.grip_rect().center().toPoint();
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, grip);
    QTest::mouseMove(editor.viewport(), target);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[2].id, std::string("todo"));
    QCOMPARE(page.blocks[3].id, std::string("list"));
    QCOMPARE(page.blocks[4].parent_id, std::string("list"));
    QCOMPARE(editor.current_block_id(), QString("list"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    editor.redo();
    QCOMPARE(editor.snapshot().blocks[3].id, std::string("list"));
}
void EditorTests::grip_drag_can_nest_and_outdent()
{
    PageEditor editor;
    ready(editor);
    editor.go_to_block("a");
    auto target = QPoint(editor.cursorRect().left() + 50, qRound(editor.active_block_rect().center().y()));
    editor.go_to_block("c");
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, editor.grip_rect().center().toPoint());
    QTest::mouseMove(editor.viewport(), target);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    QCOMPARE(ids(editor.snapshot()), QStringList({"a", "c", "b"}));
    QCOMPARE(editor.snapshot().blocks[1].parent_id, std::string("a"));
    editor.go_to_block("b");
    target = QPoint(18, qRound(editor.active_block_rect().bottom() - 2));
    editor.go_to_block("c");
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, editor.grip_rect().center().toPoint());
    QTest::mouseMove(editor.viewport(), target);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    QCOMPARE(ids(editor.snapshot()), QStringList({"a", "b", "c"}));
    QVERIFY(editor.snapshot().blocks[2].parent_id.empty());
}
void EditorTests::grip_drag_escape_and_invalid_target_do_not_edit()
{
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("child");
    const auto invalid = QPoint(editor.cursorRect().left() + 60, qRound(editor.active_block_rect().center().y()));
    editor.go_to_block("list");
    const auto grip = editor.grip_rect().center().toPoint();
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, grip);
    QTest::mouseMove(editor.viewport(), invalid);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, invalid);
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    QVERIFY(!editor.document()->isUndoAvailable());
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, grip);
    QTest::mouseMove(editor.viewport(), QPoint(18, 450));
    QTest::keyClick(&editor, Qt::Key_Escape);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(18, 450));
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    QVERIFY(!editor.document()->isUndoAvailable());
}
void EditorTests::grip_drag_scrolls_long_documents()
{
    PageEditor editor;
    ready(editor, long_page(100));
    const auto start = editor.verticalScrollBar()->value();
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, editor.grip_rect().center().toPoint());
    QTest::mouseMove(editor.viewport(), QPoint(18, editor.viewport()->height() - 5));
    QTRY_VERIFY_WITH_TIMEOUT(editor.verticalScrollBar()->value() > start, 1000);
    QTest::keyClick(&editor, Qt::Key_Escape);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton);
    QCOMPARE(ids(editor.snapshot()), ids(long_page(100)));
    QVERIFY(!editor.document()->isUndoAvailable());
}
void EditorTests::unresolved_reference_keeps_scope_and_emits_intent()
{
    PageEditor editor;
    ready(editor);
    editor.go_to_block("a");
    editor.insert_unresolved_reference("Future page", "Future page", "journal");
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[0].runs[0].reference->unresolved_name, std::string("Future page"));
    QCOMPARE(page.blocks[0].runs[0].reference->database_scope, std::string("journal"));
    QSignalSpy unresolved(&editor, &PageEditor::unresolved_reference_activated);
    QSignalSpy resolved(&editor, &PageEditor::reference_activated);
    at(editor, 2);
    const auto point = editor.cursorRect().center();
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, point);
    QCOMPARE(unresolved.count(), 1);
    QCOMPARE(resolved.count(), 0);
    QCOMPARE(unresolved[0][0].toString(), QString("Future page"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("alpha\nbravo\ncharlie"));
}
void EditorTests::divider_insertion_preserves_subtree_and_undo()
{
    PageEditor editor;
    ready(editor, sample_page());
    QVERIFY(editor.go_to_block("list"));
    editor.insert_divider();
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[3].id, std::string("child"));
    QCOMPARE(page.blocks[4].kind, BlockKind::Divider);
    QVERIFY(page.blocks[4].runs.empty());
    QCOMPARE(page.blocks[5].kind, BlockKind::Paragraph);
    QCOMPARE(editor.current_block_id().toStdString(), page.blocks[5].id);
    QCOMPARE(page.blocks[6].id, std::string("todo"));
    QVERIFY(editor.go_to_block(QString::fromStdString(page.blocks[4].id)));
    QVERIFY(editor.indent_current());
    QCOMPARE(editor.snapshot().blocks[4].parent_id, std::string("list"));
    editor.undo();
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    editor.redo();
    QCOMPARE(ids(editor.snapshot()), ids(page));
}
void EditorTests::divider_editing_becomes_a_paragraph()
{
    PageEditor editor;
    ready(editor, {"page", {{"rule", "", BlockKind::Divider, {}}, {"next", "", BlockKind::Paragraph, {{"After"}}}}});
    QTest::keyClicks(&editor, "Text");
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[0].kind, BlockKind::Paragraph);
    QCOMPARE(text(page.blocks[0]), QString("Text"));
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Divider);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.snapshot().blocks[1].kind, BlockKind::Paragraph);
}
void EditorTests::typing_undo_groups_stop_at_navigation_and_structure()
{
    PageEditor editor;
    ready(editor, {"page", {{"a", "", BlockKind::Paragraph, {}}}});
    QTest::keyClicks(&editor, "first");
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString{});
    editor.redo();
    QCOMPARE(editor.toPlainText(), QString("first"));
    QTest::keyClick(&editor, Qt::Key_Left);
    QTest::keyClick(&editor, Qt::Key_Right);
    QTest::keyClicks(&editor, "!");
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("first"));
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "second");
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("first\n"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("first"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString{});
    validate(editor.snapshot());
}
void EditorTests::rich_clipboard_preserves_tree_and_remaps_internal_references()
{
    PageEditor source;
    ready(source, sample_page());
    auto selection = source.textCursor();
    selection.select(QTextCursor::Document);
    source.setTextCursor(selection);
    source.copy();
    QVERIFY(QApplication::clipboard()->mimeData()->hasFormat("application/x-nebula-blocks+json"));
    QVERIFY(QApplication::clipboard()->text().contains("journal-notes.txt"));
    PageEditor target;
    ready(target, {"destination", {{"destination-block", "", BlockKind::Paragraph, {}}}});
    target.paste();
    const auto page = target.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), sample_page().blocks.size());
    QCOMPARE(page.blocks[0].kind, BlockKind::Heading);
    QVERIFY(page.blocks[0].runs[0].bold);
    QCOMPARE(page.blocks[3].parent_id, page.blocks[2].id);
    QCOMPARE(page.blocks[5].runs[1].reference->target_id, page.blocks[0].id);
    QCOMPARE(page.blocks[7].kind, BlockKind::Image);
    QVERIFY(page.blocks[7].runs[0].asset->image);
    for (const auto& original : sample_page().blocks)
        for (const auto& copy : page.blocks)
            QVERIFY(original.id != copy.id);
    target.undo();
    QCOMPARE(target.snapshot().blocks.size(), std::size_t(1));
    QVERIFY(target.toPlainText().isEmpty());
    target.redo();
    QCOMPARE(ids(target.snapshot()), ids(page));
}
void EditorTests::rich_clipboard_clips_unicode_and_unresolved_runs()
{
    PageEditor source;
    ready(source,
          {"source",
           {{"a",
             "",
             BlockKind::Paragraph,
             {{u8"café 🌙", true}, {"Future", false, true, Reference{ReferenceKind::Page, "", "Future", "notes"}}}}}});
    auto selection = source.textCursor();
    selection.setPosition(5);
    selection.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    source.setTextCursor(selection);
    source.copy();
    PageEditor target;
    ready(target, {"target", {{"b", "", BlockKind::Paragraph, {}}}});
    target.paste();
    const auto page = target.snapshot();
    validate(page);
    QCOMPARE(text(page.blocks[0]), QString::fromUtf8(u8"🌙Future"));
    QVERIFY(page.blocks[0].runs[0].bold);
    QVERIFY(page.blocks[0].runs[1].italic);
    QVERIFY(page.blocks[0].runs[1].reference);
    QCOMPARE(page.blocks[0].runs[1].reference->unresolved_name, std::string("Future"));
    QCOMPARE(page.blocks[0].runs[1].reference->database_scope, std::string("notes"));
}
void EditorTests::html_clipboard_keeps_supported_formatting_without_resources()
{
    auto* mime = new QMimeData;
    mime->setHtml("<h2>Title</h2><p><b>bold</b> <i>italic</i> <a href='https://example.invalid'>anchor</a><img "
                  "src='file:///tmp/not-a-nebula-asset.png'></p><ul><li>list</li></ul>");
    QApplication::clipboard()->setMimeData(mime);
    PageEditor editor;
    ready(editor, {"target", {{"b", "", BlockKind::Paragraph, {}}}});
    editor.paste();
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[0].kind, BlockKind::Heading);
    bool bold = false, italic = false, bullet = false;
    for (const auto& block : page.blocks) {
        bullet |= block.kind == BlockKind::Bullet;
        for (const auto& run : block.runs) {
            bold |= run.bold && run.text == "bold";
            italic |= run.italic && run.text == "italic";
            QVERIFY(!run.reference);
            QVERIFY(!run.asset);
        }
    }
    QVERIFY(bold);
    QVERIFY(italic);
    QVERIFY(bullet);
    QVERIFY(editor.toPlainText().contains("[image]"));
    editor.undo();
    QVERIFY(editor.toPlainText().isEmpty());
}
void EditorTests::malformed_clipboard_leaves_document_unchanged()
{
    PageEditor editor;
    ready(editor);
    const auto before = encode_document(editor.snapshot());
    auto* mime = new QMimeData;
    mime->setData("application/x-nebula-blocks+json", "{\"version\":99}");
    mime->setText("Fallback must not replace this selection");
    QApplication::clipboard()->setMimeData(mime);
    QSignalSpy rejected(&editor, &PageEditor::paste_rejected);
    auto selected = editor.textCursor();
    selected.select(QTextCursor::Document);
    editor.setTextCursor(selected);
    editor.paste();
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(encode_document(editor.snapshot()), before);
    QVERIFY(!editor.document()->isUndoAvailable());
}
void EditorTests::rich_paste_preserves_suffix_and_existing_children()
{
    PageEditor source;
    ready(source,
          {"source", {{"one", "", BlockKind::Heading, {{"ONE", true}}}, {"two", "", BlockKind::Paragraph, {{"TWO"}}}}});
    auto selection = source.textCursor();
    selection.select(QTextCursor::Document);
    source.setTextCursor(selection);
    source.copy();
    const Page original{"target",
                        {{"parent", "", BlockKind::Paragraph, {{"before after"}}},
                         {"child", "parent", BlockKind::Paragraph, {{"Keep child"}}},
                         {"next", "", BlockKind::Paragraph, {{"Keep next"}}}}};
    PageEditor target;
    ready(target, original);
    at(target, 7);
    target.paste();
    const auto page = target.snapshot();
    validate(page);
    QCOMPARE(text(page.blocks[0]), QString("before ONE"));
    QCOMPARE(text(page.blocks[1]), QString("TWOafter"));
    QCOMPARE(page.blocks[2].id, std::string("child"));
    QCOMPARE(page.blocks[2].parent_id, page.blocks[1].id);
    QCOMPARE(page.blocks[3].id, std::string("next"));
    QCOMPARE(target.textCursor().position(), int(QString("before ONE\nTWO").size()));
    target.undo();
    QCOMPARE(encode_document(target.snapshot()), encode_document(original));
}
void EditorTests::empty_list_enter_exits_without_an_extra_block()
{
    PageEditor editor;
    ready(editor, {"page", {{"item", "", BlockKind::Todo, {}, "", true}}});
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(1));
    QCOMPARE(page.blocks[0].id, std::string("item"));
    QCOMPARE(page.blocks[0].kind, BlockKind::Paragraph);
    QVERIFY(!page.blocks[0].checked);
    QVERIFY(!editor.textCursor().currentList());
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Todo);
    QVERIFY(editor.snapshot().blocks[0].checked);
    QApplication::clipboard()->setText("\ntext");
    editor.paste();
    const auto pasted = editor.snapshot();
    validate(pasted);
    QCOMPARE(pasted.blocks.size(), std::size_t(2));
    QVERIFY(text(pasted.blocks[0]).isEmpty());
    QCOMPARE(text(pasted.blocks[1]), QString("text"));
}
void EditorTests::pasted_divider_does_not_consume_surrounding_text()
{
    auto* mime = new QMimeData;
    mime->setData("application/x-nebula-blocks+json",
                  QByteArray::fromStdString(encode_document({"source", {{"rule", "", BlockKind::Divider, {}}}})));
    QApplication::clipboard()->setMimeData(mime);
    PageEditor editor;
    ready(editor, {"page", {{"a", "", BlockKind::Paragraph, {{"leftRIGHT"}}}}});
    at(editor, 4);
    editor.paste();
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(3));
    QCOMPARE(text(page.blocks[0]), QString("left"));
    QCOMPARE(page.blocks[1].kind, BlockKind::Divider);
    QCOMPARE(text(page.blocks[2]), QString("RIGHT"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("leftRIGHT"));
}

void EditorTests::load_preserves_model()
{
    PageEditor editor;
    ready(editor, sample_page());
    const auto page = editor.snapshot();
    validate(page);
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
    PageEditor editor;
    ready(editor, long_page(3));
    const auto original = ids(editor.snapshot());
    editor.resize(180, 400);
    QApplication::processEvents();
    QVERIFY(editor.document()->begin().layout()->lineCount() > 1);
    QCOMPARE(editor.document()->blockCount(), 3);
    QCOMPARE(ids(editor.snapshot()), original);
}

void EditorTests::enter_splits_with_new_identity()
{
    PageEditor editor;
    ready(editor);
    at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(4));
    QCOMPARE(page.blocks[0].id, std::string("a"));
    QVERIFY(page.blocks[1].id != "a");
    QCOMPARE(text(page.blocks[0]), QString("al"));
    QCOMPARE(text(page.blocks[1]), QString("pha"));
    QCOMPARE(page.blocks[2].id, std::string("b"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.redo();
    QCOMPARE(ids(editor.snapshot()), ids(page));
}

void EditorTests::splitting_parent_keeps_children_with_trailing_block()
{
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("list");
    auto cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextCharacter);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[4].id, std::string("child"));
    QCOMPARE(page.blocks[4].parent_id, page.blocks[3].id);
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[3].parent_id, std::string("list"));
}

void EditorTests::enter_continues_lists_and_resets_checkbox()
{
    PageEditor editor;
    ready(editor);
    editor.set_block_kind(BlockKind::Todo);
    editor.toggle_checked();
    QTest::keyClick(&editor, Qt::Key_End);
    QTest::keyClick(&editor, Qt::Key_Return);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[0].kind, BlockKind::Todo);
    QVERIFY(page.blocks[0].checked);
    QCOMPARE(page.blocks[1].kind, BlockKind::Todo);
    QVERIFY(!page.blocks[1].checked);
    QVERIFY(editor.textCursor().currentList());
    editor.undo();
    QCOMPARE(editor.snapshot().blocks.size(), std::size_t(3));
}

void EditorTests::shift_enter_keeps_identity()
{
    PageEditor editor;
    ready(editor);
    at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(ids(page), ids(short_page()));
    QVERIFY(text(page.blocks[0]).contains(QChar::LineSeparator));
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
}

void EditorTests::backspace_merges_and_undo_restores_identity()
{
    PageEditor editor;
    ready(editor);
    editor.go_to_block("b");
    QTest::keyClick(&editor, Qt::Key_Backspace);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(ids(page), QStringList({"a", "c"}));
    QCOMPARE(text(page.blocks[0]), QString("alphabravo"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.redo();
    QCOMPARE(ids(editor.snapshot()), ids(page));
}

void EditorTests::delete_merges_and_undo_restores_identity()
{
    PageEditor editor;
    ready(editor);
    at(editor, 5);
    QTest::keyClick(&editor, Qt::Key_Delete);
    validate(editor.snapshot());
    QCOMPARE(ids(editor.snapshot()), QStringList({"a", "c"}));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
}

void EditorTests::backspace_at_nested_block_start_outdents_subtree()
{
    for (const auto kind : {BlockKind::Paragraph, BlockKind::Heading, BlockKind::Bullet, BlockKind::Todo}) {
        for (const bool empty : {false, true}) {
            Page original{
                "nested-backspace",
                {{"outer", "", BlockKind::Heading, {{"Outer"}}},
                 {"parent", "outer", BlockKind::Paragraph, {{"Parent"}}},
                 {"prior", "parent", BlockKind::Paragraph, {{"First child"}}},
                 {"child", "parent", kind, empty ? std::vector<Run>{} : std::vector<Run>{{u8"日本語 café", true}}},
                 {"grandchild", "child", BlockKind::Paragraph, {{"Keep descendant"}}},
                 {"later", "parent", BlockKind::Paragraph, {{"Keep sibling"}}},
                 {"next", "", BlockKind::Paragraph, {{"Next root"}}}}};
            original.blocks[3].checked = kind == BlockKind::Todo;
            PageEditor editor;
            ready(editor, original);
            QVERIFY(editor.go_to_block("child"));
            QTest::keyClick(&editor, Qt::Key_Backspace);
            const auto outdented = editor.snapshot();
            validate(outdented);
            QCOMPARE(ids(outdented), QStringList({"outer", "parent", "prior", "later", "child", "grandchild", "next"}));
            QCOMPARE(outdented.blocks[4].parent_id, std::string("outer"));
            QCOMPARE(outdented.blocks[4].kind, kind);
            QCOMPARE(outdented.blocks[4].checked, original.blocks[3].checked);
            QCOMPARE(text(outdented.blocks[4]), text(original.blocks[3]));
            if (!empty)
                QVERIFY(outdented.blocks[4].runs[0].bold);
            QCOMPARE(outdented.blocks[5].parent_id, std::string("child"));
            QCOMPARE(outdented.blocks[3].parent_id, std::string("parent"));
            QCOMPARE(editor.current_block_id(), QString("child"));
            QCOMPARE(editor.textCursor().positionInBlock(), 0);
            QCOMPARE(editor.textCursor().blockFormat().indent(), 1);
            QVERIFY(editor.take_absorbed_blocks().empty());
            QTest::keyClick(&editor, Qt::Key_Backspace);
            QVERIFY(editor.snapshot().blocks[4].parent_id.empty());
            QCOMPARE(editor.textCursor().blockFormat().indent(), 0);
            const auto promoted = encode_document(editor.snapshot());
            editor.undo();
            QCOMPARE(encode_document(editor.snapshot()), encode_document(outdented));
            editor.undo();
            QCOMPARE(encode_document(editor.snapshot()), encode_document(original));
            QCOMPARE(editor.current_block_id(), QString("child"));
            QCOMPARE(editor.textCursor().positionInBlock(), 0);
            editor.redo();
            QCOMPARE(encode_document(editor.snapshot()), encode_document(outdented));
            editor.redo();
            QCOMPARE(encode_document(editor.snapshot()), promoted);
        }
    }
}

void EditorTests::paste_normalizes_line_endings_and_ids()
{
    PageEditor editor;
    ready(editor);
    at(editor, 5);
    QApplication::clipboard()->setText("one\r\ntwo\rthree\n");
    QTest::keyClick(&editor, Qt::Key_V, Qt::ControlModifier);
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(6));
    QCOMPARE(text(page.blocks[0]), QString("alphaone"));
    QCOMPARE(text(page.blocks[1]), QString("two"));
    QCOMPARE(text(page.blocks[2]), QString("three"));
    QVERIFY(text(page.blocks[3]).isEmpty());
    QCOMPARE(page.blocks[4].id, std::string("b"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("alpha\nbravo\ncharlie"));
}

void EditorTests::keyboard_selection_spans_blocks()
{
    PageEditor editor;
    ready(editor);
    at(editor, 2);
    QTest::keyClick(&editor, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(editor.textCursor().selectedText().contains("bravo"));
    QVERIFY(editor.textCursor().selectedText().contains("charlie"));
    editor.copy();
    QCOMPARE(QApplication::clipboard()->text(), QString("pha\nbravo\ncharlie"));
}

void EditorTests::cut_and_undo_preserve_blocks()
{
    PageEditor editor;
    ready(editor);
    at(editor, 2);
    auto cursor = editor.textCursor();
    cursor.setPosition(9, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_X, Qt::ControlModifier);
    validate(editor.snapshot());
    QCOMPARE(editor.toPlainText(), QString("alvo\ncharlie"));
    QCOMPARE(QApplication::clipboard()->text(), QString("pha\nbra"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    QCOMPARE(editor.toPlainText(), QString("alpha\nbravo\ncharlie"));
}

void EditorTests::formatting_spans_blocks_and_undoes()
{
    PageEditor editor;
    ready(editor);
    auto cursor = editor.textCursor();
    cursor.select(QTextCursor::Document);
    editor.setTextCursor(cursor);
    editor.toggle_bold();
    editor.toggle_italic();
    for (const auto& block : editor.snapshot().blocks) {
        QVERIFY(block.runs[0].bold);
        QVERIFY(block.runs[0].italic);
    }
    editor.undo();
    for (const auto& block : editor.snapshot().blocks) {
        QVERIFY(block.runs[0].bold);
        QVERIFY(!block.runs[0].italic);
    }
    editor.undo();
    QVERIFY(!editor.snapshot().blocks[0].runs[0].bold);
}

void EditorTests::block_kind_conversion_preserves_content_and_references()
{
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("reference");
    const auto original = editor.snapshot();
    for (const auto kind : {BlockKind::Heading, BlockKind::Bullet, BlockKind::Paragraph}) {
        editor.set_block_kind(kind);
        const auto page = editor.snapshot();
        validate(page);
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
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("list");
    QTest::keyClick(&editor, Qt::Key_Down, Qt::AltModifier);
    const auto moved = editor.snapshot();
    validate(moved);
    QCOMPARE(moved.blocks[2].id, std::string("todo"));
    QCOMPARE(moved.blocks[3].id, std::string("list"));
    QCOMPARE(moved.blocks[4].id, std::string("child"));
    QCOMPARE(editor.current_block_id(), QString("list"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
    editor.redo();
    QCOMPARE(ids(editor.snapshot()), ids(moved));
    QVERIFY(editor.snapshot().blocks[5].runs[1].reference.has_value());
}

void EditorTests::indent_and_outdent_with_keyboard()
{
    PageEditor editor;
    ready(editor);
    editor.go_to_block("b");
    QTest::keyClick(&editor, Qt::Key_Tab);
    auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[1].parent_id, std::string("a"));
    QTest::keyClick(&editor, Qt::Key_Backtab, Qt::ShiftModifier);
    page = editor.snapshot();
    validate(page);
    QVERIFY(page.blocks[1].parent_id.empty());
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[1].parent_id, std::string("a"));
}

void EditorTests::deleting_parent_repairs_surviving_children()
{
    PageEditor editor;
    ready(editor, sample_page());
    editor.go_to_block("list");
    auto cursor = editor.textCursor();
    cursor.setPosition(cursor.block().next().position(), QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.cut();
    validate(editor.snapshot());
    editor.undo();
    validate(editor.snapshot());
    QCOMPARE(ids(editor.snapshot()), ids(sample_page()));
}

void EditorTests::references_keep_identity_and_do_not_leak()
{
    PageEditor editor;
    ready(editor);
    at(editor, 0);
    editor.insert_reference("heading", ReferenceKind::Block, "c");
    QTest::keyClicks(&editor, " after");
    const auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[0].runs[0].reference->target_id, std::string("c"));
    QVERIFY(!page.blocks[0].runs.back().reference.has_value());
    editor.go_to_block("c");
    QTest::keyClicks(&editor, "Renamed ");
    QCOMPARE(editor.snapshot().blocks[0].runs[0].reference->target_id, std::string("c"));
    QVERIFY(editor.go_to_block("c"));
    QVERIFY(!editor.go_to_block("missing"));
}

void EditorTests::reference_can_be_followed_by_mouse()
{
    PageEditor editor;
    ready(editor);
    editor.insert_reference("jump", ReferenceKind::Block, "b");
    at(editor, 1);
    const auto point = editor.cursorRect().center();
    QSignalSpy spy(&editor, &PageEditor::reference_activated);
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, point);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), static_cast<int>(ReferenceKind::Block));
    QCOMPARE(spy[0][1].toString(), QString("b"));
}

void EditorTests::todo_state_undoes()
{
    PageEditor editor;
    ready(editor);
    editor.set_block_kind(BlockKind::Todo);
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Todo);
    editor.toggle_checked();
    QVERIFY(editor.snapshot().blocks[0].checked);
    editor.undo();
    QVERIFY(!editor.snapshot().blocks[0].checked);
    editor.undo();
    QCOMPARE(editor.snapshot().blocks[0].kind, BlockKind::Paragraph);
}

void EditorTests::cards_are_selectable_and_undoable()
{
    PageEditor editor;
    ready(editor);
    editor.insert_card(true, "sample");
    auto page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks.size(), std::size_t(4));
    QVERIFY(page.blocks[1].runs[0].asset->image);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    page = editor.snapshot();
    validate(page);
    QCOMPARE(page.blocks[1].kind, BlockKind::Paragraph);
    editor.undo();
    QVERIFY(editor.snapshot().blocks[1].runs[0].asset.has_value());
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.insert_card(false, "notes.txt");
    QVERIFY(!editor.snapshot().blocks[1].runs[0].asset->image);
}

void EditorTests::editing_around_card_survives_reorder()
{
    PageEditor editor;
    ready(editor);
    editor.insert_card(false, "notes.txt");
    QTest::keyClicks(&editor, " suffix");
    const auto before = editor.toPlainText();
    QVERIFY(editor.move_current(1));
    QVERIFY(editor.move_current(-1));
    QCOMPARE(editor.toPlainText(), before);
    validate(editor.snapshot());
}

void EditorTests::input_method_preedit_and_commit()
{
    PageEditor editor;
    ready(editor);
    at(editor, 0);
    QSignalSpy changes(&editor, &PageEditor::content_changed);
    QInputMethodEvent preedit(QString::fromUtf8(u8"に"), {});
    QApplication::sendEvent(&editor, &preedit);
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
    QCOMPARE(changes.count(), 0);
    QInputMethodEvent update(QString::fromUtf8(u8"にほ"), {});
    QApplication::sendEvent(&editor, &update);
    QInputMethodEvent cancel;
    QApplication::sendEvent(&editor, &cancel);
    QCOMPARE(changes.count(), 0);
    QVERIFY(!editor.document()->isUndoAvailable());
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8(u8"日本語🌙"));
    QApplication::sendEvent(&editor, &commit);
    QCOMPARE(text(editor.snapshot().blocks[0]), QString::fromUtf8(u8"日本語🌙alpha"));
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
    // An empty commit may still delete a replacement range.
    at(editor, 1);
    QInputMethodEvent replacement;
    replacement.setCommitString({}, -1, 1);
    QApplication::sendEvent(&editor, &replacement);
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("lpha"));
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
    QCOMPARE(ids(editor.snapshot()), ids(short_page()));
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
    PageEditor editor;
    ready(editor);
    auto* accessible = QAccessible::queryAccessibleInterface(&editor);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::EditableText);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Page block editor"));
    QVERIFY(accessible->textInterface());
}

void EditorTests::comparison_selection_stays_in_one_widget()
{
    BlockWidgets comparison;
    comparison.set_page(short_page());
    comparison.resize(700, 500);
    comparison.show();
    QApplication::processEvents();
    auto* first = comparison.editors()[0];
    first->setFocus();
    QTest::keyClick(first, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(first->textCursor().selectedText(), QString("alpha"));
    QVERIFY(!comparison.editors()[1]->textCursor().hasSelection());
    QVERIFY(!comparison.editors()[2]->textCursor().hasSelection());
}

void EditorTests::comparison_undo_is_per_widget()
{
    BlockWidgets comparison;
    comparison.set_page(short_page());
    auto* first = comparison.editors()[0];
    auto* second = comparison.editors()[1];
    first->insertPlainText("1");
    second->insertPlainText("2");
    second->undo();
    QCOMPARE(first->toPlainText(), QString("1alpha"));
    QCOMPARE(second->toPlainText(), QString("bravo"));
    first->undo();
    QCOMPARE(first->toPlainText(), QString("alpha"));
}

void EditorTests::long_page_edit_remains_valid()
{
    PageEditor editor;
    ready(editor, long_page(1000));
    editor.go_to_block("block-500");
    QTest::keyClick(&editor, Qt::Key_Return);
    validate(editor.snapshot());
    QCOMPARE(editor.document()->blockCount(), 1001);
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(long_page(1000)));
}

void EditorTests::structural_commands_keep_caret_offset_and_selection()
{
    PageEditor editor;
    ready(editor);
    at(editor, 6 + 3); // Inside "bravo".
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.current_block_id(), QString("b"));
    QCOMPARE(editor.textCursor().positionInBlock(), 3);
    QTest::keyClicks(&editor, "X");
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("braXvo"));
    QTextCursor selection = editor.textCursor();
    selection.setPosition(editor.textCursor().block().position() + 1);
    selection.setPosition(editor.textCursor().block().position() + 4, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    QTest::keyClick(&editor, Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(editor.textCursor().selectedText(), QString("raX"));
    QTest::keyClick(&editor, Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(ids(editor.snapshot()), QStringList({"b", "a", "c"}));
    QCOMPARE(editor.textCursor().selectedText(), QString("raX"));
}
void EditorTests::structural_commands_rewrite_only_the_affected_range()
{
    PageEditor editor;
    ready(editor, long_page(2000));
    editor.go_to_block("block-1000");
    const auto length = editor.document()->characterCount();
    int changed = 0;
    connect(editor.document(), &QTextDocument::contentsChange, &editor,
            [&](int, int removed, int added) { changed += removed + added; });
    QTest::keyClick(&editor, Qt::Key_Down, Qt::AltModifier);
    QVERIFY2(changed > 0 && changed < length / 50, qPrintable(QString::number(changed)));
    QCOMPARE(QString::fromStdString(editor.snapshot().blocks[1001].id), QString("block-1000"));
    editor.undo();
    QCOMPARE(ids(editor.snapshot()), ids(long_page(2000)));
    QCOMPARE(editor.current_block_id(), QString("block-1000")); // Undo returns to the moved block.
}
void EditorTests::plain_text_paste_drops_rich_formats()
{
    PageEditor editor;
    ready(editor);
    at(editor, 5);
    auto* mime = new QMimeData;
    mime->setHtml("<b>bold</b><h1>head</h1>");
    mime->setText("bold");
    QApplication::clipboard()->setMimeData(mime);
    QTest::keyClick(&editor, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    const auto page = editor.snapshot();
    QCOMPARE(page.blocks.size(), std::size_t(3));
    QCOMPARE(text(page.blocks[0]), QString("alphabold"));
    for (const auto& run : page.blocks[0].runs)
        QVERIFY(!run.bold);
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("alpha"));
}
void EditorTests::merged_blocks_are_reported_as_absorbed()
{
    PageEditor editor;
    ready(editor);
    editor.go_to_block("b");
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(editor.take_absorbed_blocks(), (std::unordered_set<std::string>{"b"}));
    QVERIFY(editor.take_absorbed_blocks().empty());
    at(editor, 10);
    QTest::keyClick(&editor, Qt::Key_Delete);
    editor.undo();
    QVERIFY(editor.take_absorbed_blocks().empty());
    Page list{"list", {{"x", "", BlockKind::Bullet, {{"one"}}}, {"y", "", BlockKind::Bullet, {{"two"}}}}};
    ready(editor, list);
    editor.go_to_block("y");
    QTest::keyClick(&editor, Qt::Key_Backspace);
    const auto absorbed = editor.take_absorbed_blocks();
    const auto live = ids(editor.snapshot());
    for (const auto& id : absorbed)
        QVERIFY(!live.contains(QString::fromStdString(id)));
}
void EditorTests::block_state_matches_core_operations()
{
    PageEditor editor;
    ready(editor, sample_page());
    for (const auto& block : sample_page().blocks) {
        editor.go_to_block(QString::fromStdString(block.id));
        const auto state = editor.block_state();
        auto page = editor.snapshot();
        auto copy = page;
        const bool up = move_subtree(copy, block.id, -1);
        copy = page;
        const bool down = move_subtree(copy, block.id, 1);
        copy = page;
        const bool in = indent(copy, block.id);
        copy = page;
        const bool out = outdent(copy, block.id);
        QCOMPARE(state.up, up);
        QCOMPARE(state.down, down);
        QCOMPARE(state.indent, in);
        QCOMPARE(state.outdent, out);
        QCOMPARE(state.depth, depth(page, block.id));
        QCOMPARE(state.id, block.id);
    }
}
void EditorTests::ordinary_typing_reuses_cached_external_labels()
{
    PageEditor editor;
    int calls = 0;
    editor.set_reference_label_resolver([&](const Reference&) -> std::optional<std::string> {
        ++calls;
        return std::string("Elsewhere");
    });
    ready(editor, Page{"p",
                       {{"a",
                         "",
                         BlockKind::Paragraph,
                         {{"old", false, false, Reference{ReferenceKind::Page, "other", {}, {}, false}}}},
                        {"b", "", BlockKind::Paragraph, {{"plain"}}}}});
    QCOMPARE(text(editor.snapshot().blocks[0]), QString("Elsewhere"));
    const auto initial = calls;
    editor.go_to_block("b");
    QTest::keyClicks(&editor, "typing several characters");
    QCOMPARE(calls, initial);
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("typing several charactersplain"));
    editor.undo();
    QCOMPARE(text(editor.snapshot().blocks[1]), QString("plain"));
}

QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
