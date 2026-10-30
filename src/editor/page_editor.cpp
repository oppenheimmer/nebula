#include "nebula/editor/page_editor.hpp"
#include "nebula/format/codec.hpp"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextObjectInterface>
#include <QUuid>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <QCompleter>
#include <QAbstractItemView>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <memory>

namespace nebula {
namespace {

constexpr int Id = QTextFormat::UserProperty + 1;
constexpr int ParentId = QTextFormat::UserProperty + 2;
constexpr int Kind = QTextFormat::UserProperty + 3;
constexpr int AssetLabel = QTextFormat::UserProperty + 4;
constexpr int Checked = QTextFormat::UserProperty + 5;
constexpr int RefKind = QTextFormat::UserProperty + 6;
constexpr int RefTarget = QTextFormat::UserProperty + 7;
constexpr int CardImage = QTextFormat::UserProperty + 8;
constexpr int RefName = QTextFormat::UserProperty + 9;
constexpr int RefScope = QTextFormat::UserProperty + 10;
constexpr int CardObject = QTextFormat::UserObject + 1;
constexpr auto ClipboardFormat = "application/x-nebula-blocks+json";

QString new_id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

class CardRenderer final : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit CardRenderer(QObject* parent) : QObject(parent) {}
    QSizeF intrinsicSize(QTextDocument*, int, const QTextFormat& format) override
    {
        return {340, format.boolProperty(CardImage) ? 100.0 : 48.0};
    }
    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument*, int,
                    const QTextFormat& format) override
    {
        painter->save();
        const bool image = format.boolProperty(CardImage);
        painter->setPen(QColor("#a6bac3"));
        painter->setBrush(QColor(image ? "#e3eff0" : "#f0f2f4"));
        painter->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 6, 6);
        if (image) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#74a5a4"));
            painter->drawEllipse(QPointF(rect.left() + 35, rect.top() + 27), 12, 12);
            painter->drawPolygon(QPolygonF({{rect.left() + 12, rect.bottom() - 12},
                {rect.left() + 66, rect.top() + 35}, {rect.left() + 104, rect.bottom() - 12}}));
        }
        painter->setPen(QColor("#263c45"));
        painter->drawText(rect.adjusted(image ? 115 : 16, 8, -10, -8),
                          Qt::AlignVCenter | Qt::TextWordWrap,
                          format.stringProperty(AssetLabel));
        painter->restore();
    }
};

QTextCharFormat run_format(const Run& run)
{
    QTextCharFormat format;
    format.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(run.italic);
    if (run.reference) {
        format.setAnchor(true);
        format.setAnchorHref("nebula:" + QString::fromStdString(run.reference->target_id));
        format.setProperty(RefKind, static_cast<int>(run.reference->kind));
        format.setProperty(RefTarget, QString::fromStdString(run.reference->target_id));
        format.setProperty(RefName, QString::fromStdString(run.reference->unresolved_name));
        format.setProperty(RefScope, QString::fromStdString(run.reference->database_scope));
        format.setForeground(QColor(run.reference->target_id.empty()?"#a16b3b":"#236b9a"));
        format.setFontUnderline(true);
    }
    if (run.asset) {
        format.setObjectType(CardObject);
        format.setProperty(CardImage, run.asset->image);
        format.setProperty(AssetLabel, QString::fromStdString(run.asset->label));
    }
    return format;
}

Block read_block(const QTextBlock& qt_block, int start=0, int end=std::numeric_limits<int>::max())
{
    const auto format=qt_block.blockFormat();
    Block block{format.stringProperty(Id).toStdString(),format.stringProperty(ParentId).toStdString(),
        BlockKind(format.intProperty(Kind)),{},format.stringProperty(AssetLabel).toStdString(),format.boolProperty(Checked)};
    for (auto it=qt_block.begin();!it.atEnd();++it) {
        const auto fragment=it.fragment();
        if (!fragment.isValid()) continue;
        const auto from=std::max(start,fragment.position()), to=std::min(end,fragment.position()+fragment.length());
        if (from>=to) continue;
        const auto character=fragment.charFormat();
        Run run{fragment.text().mid(from-fragment.position(),to-from).toStdString(),
            character.fontWeight()>=QFont::Bold,character.fontItalic()};
        if (character.hasProperty(RefTarget)) run.reference=Reference{ReferenceKind(character.intProperty(RefKind)),
            character.stringProperty(RefTarget).toStdString(),character.stringProperty(RefName).toStdString(),character.stringProperty(RefScope).toStdString()};
        if (character.objectType()==CardObject) run.asset=Asset{character.boolProperty(CardImage),character.stringProperty(AssetLabel).toStdString()};
        block.runs.push_back(std::move(run));
    }
    return block;
}

void apply_block_format(QTextCursor& cursor,const Block& block,int level)
{
    if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
    QTextBlockFormat format;
    format.setProperty(Id,QString::fromStdString(block.id));
    format.setProperty(ParentId,QString::fromStdString(block.parent_id));
    format.setProperty(Kind,int(block.kind)); format.setProperty(AssetLabel,QString::fromStdString(block.asset_label));
    format.setProperty(Checked,block.checked); format.setIndent(level); format.setBottomMargin(10);
    format.setHeadingLevel(block.kind==BlockKind::Heading?1:0); cursor.setBlockFormat(format);
    if (block.kind==BlockKind::Bullet || block.kind==BlockKind::Todo) {
        QTextListFormat list; list.setStyle(QTextListFormat::ListDisc); list.setIndent(level+1); cursor.createList(list);
        if (block.kind==BlockKind::Todo) {
            auto marked=cursor.blockFormat();
            marked.setMarker(block.checked?QTextBlockFormat::MarkerType::Checked:QTextBlockFormat::MarkerType::Unchecked);
            cursor.setBlockFormat(marked);
        }
    }
}

// Pasted HTML never fetches images, stylesheets, or other resources.
class ClipboardDocument : public QTextDocument {
protected:
    QVariant loadResource(int,const QUrl&) override { return {}; }
};

Page html_fragment(const QString& html)
{
    ClipboardDocument document; document.setHtml(html); Page page{"clipboard",{}};
    for (auto qt=document.begin();qt.isValid();qt=qt.next()) {
        auto block=read_block(qt); block.id=new_id().toStdString(); block.parent_id.clear();
        block.kind=qt.blockFormat().headingLevel()>0?BlockKind::Heading:(qt.textList()?BlockKind::Bullet:BlockKind::Paragraph);
        for (auto& run:block.runs) { auto text=QString::fromStdString(run.text); text.replace(QChar::ObjectReplacementCharacter,"[image]"); run.text=text.toStdString(); }
        page.blocks.push_back(std::move(block));
    }
    validate(page); return page;
}

} // namespace

PageEditor::PageEditor(QWidget* parent) : QTextEdit(parent)
{
    setAccessibleName("Page block editor");
    setAccessibleDescription("Flowing block document. Drag the grip dots to move a subtree. Alt+Up or Down moves a block; Tab nests it.");
    setAcceptDrops(false); // Native internal text drag needs identity-aware handling in M05.
    setTabChangesFocus(false);
    setFont(QFont("Sans Serif", 12));
    setPlaceholderText("Write a note…");
    document()->setDocumentMargin(32);
    viewport()->setMouseTracking(true);
    drag_scroll_=new QTimer(this);
    drag_scroll_->setInterval(50);
    connect(drag_scroll_,&QTimer::timeout,this,[this] {
        if (!dragging_) return;
        int amount=0;
        if (drag_position_.y()<24) amount=-20;
        else if (drag_position_.y()>viewport()->height()-24) amount=20;
        if (amount) { verticalScrollBar()->setValue(verticalScrollBar()->value()+amount); update_drop(); }
    });
    document()->documentLayout()->registerHandler(CardObject, new CardRenderer(this));
    connect(this, &QTextEdit::cursorPositionChanged, viewport(), QOverload<>::of(&QWidget::update));
    connect(this, &QTextEdit::textChanged, viewport(), QOverload<>::of(&QWidget::update));
    set_page({"untitled", {{"first-block", "", BlockKind::Paragraph, {}}}});
}

QRectF PageEditor::active_block_rect() const
{
    return block_rect(textCursor().block());
}

QRectF PageEditor::block_rect(const QTextBlock& block) const
{
    const auto bounds = document()->documentLayout()->blockBoundingRect(block);
    const auto* layout = block.layout();
    const qreal line_y = layout && layout->lineCount() ? layout->lineAt(0).y() : 0;
    const qreal top = cursorRect(QTextCursor(block)).top() - line_y;
    return {8, top - 4, qreal(viewport()->width() - 16), bounds.height() + 8};
}

QRectF PageEditor::grip_rect() const { const auto rect=active_block_rect(); return {10,rect.top()+4,16,18}; }

void PageEditor::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    auto rule=palette().color(QPalette::Text); rule.setAlpha(90);
    painter.setPen(QPen(rule,1));
    for (auto block=document()->begin();block.isValid();block=block.next()) {
        if (block.blockFormat().intProperty(Kind)!=int(BlockKind::Divider)) continue;
        const auto bounds=block_rect(block);
        if (bounds.bottom()<0 || bounds.top()>viewport()->height()) continue;
        painter.drawLine(QPointF(32,bounds.center().y()),QPointF(viewport()->width()-32,bounds.center().y()));
    }
    const auto rect = active_block_rect();
    painter.setBrush(Qt::NoBrush);
    // A quiet one-pixel outline and a small shadow; leave text selection native.
    auto shadow = palette().color(QPalette::Text);
    shadow.setAlpha(14);
    painter.setPen(QPen(shadow, 1));
    painter.drawRoundedRect(rect.translated(0, 1.5), 4, 4);
    auto outline = palette().color(QPalette::Text);
    outline.setAlpha(55);
    painter.setPen(QPen(outline, 1));
    painter.drawRoundedRect(rect, 4, 4);
    auto dots=palette().color(QPalette::Text); dots.setAlpha(isEnabled()?110:35);
    painter.setPen(Qt::NoPen); painter.setBrush(dots);
    const auto grip=grip_rect();
    for (int row=0;row<3;++row) for (int column=0;column<2;++column)
        painter.drawEllipse(QPointF(grip.left()+5+column*5,grip.top()+4+row*5),1.2,1.2);
    if (dragging_) {
        painter.setPen(QPen(drop_valid_?QColor("#477b9c"):QColor("#b56a65"),2));
        painter.drawLine(drop_marker_,QPointF(viewport()->width()-10,drop_marker_.y()));
    }
}

void PageEditor::set_page(const Page& page)
{
    if (wiki_completer_) wiki_completer_->popup()->hide();
    cancel_drag();
    validate(page);
    page_id_ = page.id;
    write_page(page);
    document()->clearUndoRedoStacks();
    document()->setModified(false);
}

void PageEditor::write_page(const Page& page, const QString& focus_id)
{
    typing_position_=-1;
    validate(page);
    std::unordered_map<std::string, int> depths;
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    bool first = true;
    for (const auto& block : page.blocks) {
        const int level = block.parent_id.empty() ? 0 : depths.at(block.parent_id) + 1;
        depths.emplace(block.id, level);
        QTextBlockFormat format;
        format.setProperty(Id, QString::fromStdString(block.id));
        format.setProperty(ParentId, QString::fromStdString(block.parent_id));
        format.setProperty(Kind, static_cast<int>(block.kind));
        format.setProperty(AssetLabel, QString::fromStdString(block.asset_label));
        format.setProperty(Checked, block.checked);
        format.setIndent(level);
        format.setBottomMargin(10);
        format.setHeadingLevel(block.kind == BlockKind::Heading ? 1 : 0);
        if (first) {
            if (cursor.currentList())
                cursor.currentList()->remove(cursor.block());
            cursor.setBlockFormat(format);
            first = false;
        } else {
            cursor.insertBlock(format, QTextCharFormat{});
        }
        if (block.kind == BlockKind::Bullet || block.kind == BlockKind::Todo) {
            QTextListFormat list;
            list.setStyle(QTextListFormat::ListDisc);
            list.setIndent(level + 1);
            cursor.createList(list);
            if (block.kind == BlockKind::Todo) {
                auto marked = cursor.blockFormat();
                marked.setMarker(block.checked ? QTextBlockFormat::MarkerType::Checked
                                               : QTextBlockFormat::MarkerType::Unchecked);
                cursor.setBlockFormat(marked);
            }
        }
        if (block.runs.empty() && (block.kind == BlockKind::Image || block.kind == BlockKind::Attachment)) {
            Run run;
            run.asset = Asset{block.kind == BlockKind::Image, block.asset_label};
            cursor.insertText(QString(QChar::ObjectReplacementCharacter), run_format(run));
        } else {
            for (const auto& run : block.runs) {
                cursor.insertText(run.asset ? QString(QChar::ObjectReplacementCharacter)
                                            : QString::fromStdString(run.text), run_format(run));
            }
        }
        cursor.setCharFormat(QTextCharFormat{});
    }
    cursor.endEditBlock();
    cursor.movePosition(QTextCursor::Start);
    setTextCursor(cursor);
    if (!focus_id.isEmpty())
        go_to_block(focus_id);
}

Page PageEditor::snapshot() const
{
    Page page{page_id_, {}};
    for (auto block=document()->begin();block.isValid();block=block.next()) page.blocks.push_back(read_block(block));
    return page;
}

QString PageEditor::current_block_id() const { return textCursor().blockFormat().stringProperty(Id); }

EditorPosition PageEditor::capture_position() const
{
    const auto cursor=textCursor(); auto anchor=cursor; anchor.setPosition(cursor.anchor());
    return {current_block_id().toStdString(),anchor.blockFormat().stringProperty(Id).toStdString(),
        cursor.positionInBlock(),anchor.positionInBlock(),verticalScrollBar()->value()};
}
bool PageEditor::restore_position(const EditorPosition& position)
{
    auto locate=[&](const std::string& id) {
        for (auto block=document()->begin();block.isValid();block=block.next())
            if (block.blockFormat().stringProperty(Id).toStdString()==id) return block;
        return QTextBlock{};
    };
    const auto block=locate(position.block), anchor=locate(position.anchor_block);
    if (!block.isValid()) { go_to_block(document()->begin().blockFormat().stringProperty(Id)); return false; }
    QTextCursor cursor(document());
    const auto end=block.position()+std::clamp(position.offset,0,block.length()-1);
    cursor.setPosition(anchor.isValid()?anchor.position()+std::clamp(position.anchor_offset,0,anchor.length()-1):end);
    cursor.setPosition(end,QTextCursor::KeepAnchor); setTextCursor(cursor); ensureCursorVisible();
    verticalScrollBar()->setValue(position.scroll); return true;
}
std::optional<Reference> PageEditor::current_reference() const
{
    auto cursor=textCursor();
    if (cursor.hasSelection()) cursor.setPosition(cursor.selectionStart());
    const auto before=cursor.charFormat();
    if (cursor.positionInBlock()<cursor.block().length()-1) cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
    auto format=cursor.charFormat();
    if (!format.hasProperty(RefTarget)) format=before;
    if (!format.hasProperty(RefTarget)) return {};
    return Reference{ReferenceKind(format.intProperty(RefKind)),format.stringProperty(RefTarget).toStdString(),
        format.stringProperty(RefName).toStdString(),format.stringProperty(RefScope).toStdString()};
}
bool PageEditor::activate_current_reference()
{
    const auto reference=current_reference(); if (!reference) return false;
    if (reference->target_id.empty()) emit unresolved_reference_activated(QString::fromStdString(reference->unresolved_name),QString::fromStdString(reference->database_scope));
    else emit reference_activated(int(reference->kind),QString::fromStdString(reference->target_id));
    return true;
}

bool PageEditor::go_to_block(const QString& id)
{
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().stringProperty(Id) == id) {
            setTextCursor(QTextCursor(block));
            ensureCursorVisible();
            return true;
        }
    }
    return false;
}

void PageEditor::mutate(const std::function<void()>& action, bool join_typing)
{
    typing_position_=-1;
    const auto before = snapshot();
    QTextCursor group = textCursor();
    if (join_typing) group.joinPreviousEditBlock();
    else group.beginEditBlock();
    action();
    normalize(before);
    group.endEditBlock();
}

void PageEditor::normalize(const Page& before)
{
    std::unordered_map<std::string, std::string> previous_parents;
    for (const auto& block : before.blocks)
        previous_parents.emplace(block.id, block.parent_id);
    std::unordered_set<std::string> seen;
    std::vector<std::string> ancestors;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        auto format = block.blockFormat();
        auto id = format.stringProperty(Id).toStdString();
        if (id.empty() || seen.count(id)) {
            id = new_id().toStdString();
            format.setProperty(Id, QString::fromStdString(id));
        }
        seen.insert(id);
        auto parent = format.stringProperty(ParentId).toStdString();
        while (!parent.empty() && std::find(ancestors.begin(), ancestors.end(), parent) == ancestors.end()) {
            const auto found = previous_parents.find(parent);
            parent = found == previous_parents.end() ? std::string{} : found->second;
        }
        while (!ancestors.empty() && ancestors.back() != parent)
            ancestors.pop_back();
        format.setProperty(ParentId, QString::fromStdString(parent));
        format.setIndent(static_cast<int>(ancestors.size()));
        ancestors.push_back(id);
        const auto kind = static_cast<BlockKind>(format.intProperty(Kind));
        if (kind==BlockKind::Divider && !block.text().isEmpty())
            format.setProperty(Kind,int(BlockKind::Paragraph));
        if ((kind == BlockKind::Image || kind == BlockKind::Attachment)
            && !block.text().contains(QChar::ObjectReplacementCharacter)) {
            format.setProperty(Kind, static_cast<int>(BlockKind::Paragraph));
            format.clearProperty(AssetLabel);
        }
        if (format != block.blockFormat()) {
            QTextCursor cursor(block);
            cursor.setBlockFormat(format);
        }
    }
}

void PageEditor::split_block(QTextCursor& cursor, bool exit_empty_list)
{
    cursor.removeSelectedText();
    auto format = cursor.blockFormat();
    const auto original_id = format.stringProperty(Id).toStdString();
    const auto trailing_id = new_id();
    const auto original_kind = static_cast<BlockKind>(format.intProperty(Kind));
    const bool list = original_kind == BlockKind::Bullet || original_kind == BlockKind::Todo;
    if (exit_empty_list && list && cursor.block().text().isEmpty()) {
        if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
        format.clearProperty(QTextFormat::ObjectIndex);
        format.setProperty(Kind,int(BlockKind::Paragraph)); format.setProperty(Checked,false);
        format.setMarker(QTextBlockFormat::MarkerType::NoMarker); cursor.setBlockFormat(format);
        return;
    }
    format.setProperty(Id, trailing_id);
    format.setProperty(Kind, static_cast<int>(list ? original_kind : BlockKind::Paragraph));
    format.setHeadingLevel(0);
    format.setMarker(original_kind == BlockKind::Todo ? QTextBlockFormat::MarkerType::Unchecked
                                                     : QTextBlockFormat::MarkerType::NoMarker);
    format.clearProperty(AssetLabel);
    format.setProperty(Checked, false);
    cursor.insertBlock(format, QTextCharFormat{});
    if (!list && cursor.currentList())
        cursor.currentList()->remove(cursor.block());
    // Existing children follow the trailing half. Keeping the new sibling before
    // those children otherwise closes their former ancestor in preorder.
    std::unordered_set<std::string> descendants{original_id};
    for (auto next = cursor.block().next(); next.isValid(); next = next.next()) {
        auto child_format = next.blockFormat();
        const auto parent = child_format.stringProperty(ParentId).toStdString();
        if (!descendants.count(parent))
            break;
        descendants.insert(child_format.stringProperty(Id).toStdString());
        if (parent == original_id) {
            child_format.setProperty(ParentId, trailing_id);
            QTextCursor child(next);
            child.setBlockFormat(child_format);
        }
    }
}

void PageEditor::keyPressEvent(QKeyEvent* event)
{
    if (wiki_completer_ && wiki_completer_->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape:
        case Qt::Key_Tab: case Qt::Key_Backtab: event->ignore(); return;
        default: break;
        }
    }
    if (grip_pressed_) {
        if (event->key()==Qt::Key_Escape) cancel_drag();
        event->accept(); return;
    }
    const auto modifiers = event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const auto key=event->key();
    const bool modifier_key=key==Qt::Key_Control || key==Qt::Key_Shift || key==Qt::Key_Alt
        || key==Qt::Key_Meta || key==Qt::Key_AltGr || key==Qt::Key_CapsLock
        || key==Qt::Key_NumLock || key==Qt::Key_ScrollLock;
    const bool navigation_key=key==Qt::Key_Left || key==Qt::Key_Right || key==Qt::Key_Up
        || key==Qt::Key_Down || key==Qt::Key_Home || key==Qt::Key_End
        || key==Qt::Key_PageUp || key==Qt::Key_PageDown;
    if (modifier_key || navigation_key || event->matches(QKeySequence::Copy)
        || event->matches(QKeySequence::SelectAll)) {
        // An empty QTextCursor edit group still emits textChanged. Navigation
        // and shortcut modifiers must not create pending edits/autosave revisions.
        // Alt+arrows below are structural commands and still need their own undo.
        if (!(modifiers==Qt::AltModifier && (key==Qt::Key_Up || key==Qt::Key_Down))) {
            typing_position_=-1;
            QTextEdit::keyPressEvent(event); return;
        }
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        typing_position_=-1;
        QTextEdit::keyPressEvent(event);
        return;
    }
    if (modifiers == Qt::AltModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        move_current(event->key() == Qt::Key_Up ? -1 : 1);
        return;
    }
    if (modifiers==Qt::ControlModifier && (key==Qt::Key_Return || key==Qt::Key_Enter)) {
        typing_position_=-1; activate_current_reference(); return;
    }
    if (event->key() == Qt::Key_Tab && modifiers == Qt::NoModifier) {
        indent_current();
        return;
    }
    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && modifiers == Qt::ShiftModifier)) {
        outdent_current();
        return;
    }
    const bool typing=!(modifiers & (Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))
        && !event->text().isEmpty() && event->text()[0].isPrint();
    const bool join=typing && !textCursor().hasSelection() && typing_clock_.isValid()
        && typing_clock_.elapsed()<1000 && typing_position_==textCursor().position()
        && typing_revision_==document()->revision();
    mutate([&] {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            auto cursor = textCursor();
            if (event->modifiers() & Qt::ShiftModifier)
                cursor.insertText(QString(QChar::LineSeparator));
            else
                split_block(cursor,true);
            setTextCursor(cursor);
        } else {
            QTextEdit::keyPressEvent(event);
        }
    },join);
    if (typing) { typing_position_=textCursor().position(); typing_revision_=document()->revision(); typing_clock_.restart(); }
    if (typing || event->key()==Qt::Key_Backspace || event->key()==Qt::Key_Delete) process_wiki_input();
}

void PageEditor::inputMethodEvent(QInputMethodEvent* event)
{
    mutate([&] { QTextEdit::inputMethodEvent(event); });
    if (!event->commitString().isEmpty()) process_wiki_input();
}

void PageEditor::set_wiki_completer(QCompleter* completer)
{
    wiki_completer_=completer;
    if (completer) completer->setWidget(this);
}
int PageEditor::wiki_start() const
{
    const auto cursor=textCursor();
    if (cursor.hasSelection() || cursor.charFormat().hasProperty(RefTarget)) return -1;
    const auto text=cursor.block().text().left(cursor.positionInBlock());
    const auto start=text.lastIndexOf("[[");
    if (start<0) return -1;
    int escapes=0; for (auto i=start;i>0 && text[i-1]=='\\';--i) ++escapes;
    if (escapes%2 || text.mid(start+2).contains("]]")) return -1;
    return cursor.block().position()+int(start);
}
std::optional<QString> PageEditor::wiki_prefix() const
{
    const auto start=wiki_start();
    if (start<0) return {};
    auto cursor=textCursor(); cursor.setPosition(start+2,QTextCursor::KeepAnchor);
    return cursor.selectedText();
}
bool PageEditor::select_wiki_link()
{
    const auto start=wiki_start(); if (start<0) return false;
    auto cursor=textCursor(); const auto end=cursor.position(); cursor.setPosition(start);
    cursor.setPosition(end,QTextCursor::KeepAnchor); setTextCursor(cursor); return true;
}
void PageEditor::process_wiki_input()
{
    const auto cursor=textCursor();
    const auto text=cursor.block().text().left(cursor.positionInBlock());
    if (!cursor.hasSelection() && text.endsWith("]]")) {
        const auto start=text.lastIndexOf("[[");
        int escapes=0; for (auto i=start;i>0 && text[i-1]=='\\';--i) ++escapes;
        if (start>=0 && !(escapes%2) && receivers(SIGNAL(wiki_link_requested(QString)))) {
            if (wiki_completer_) wiki_completer_->popup()->hide();
            auto selected=cursor; selected.setPosition(cursor.block().position()+int(start));
            selected.setPosition(cursor.position(),QTextCursor::KeepAnchor);
            setTextCursor(selected); emit wiki_link_requested(selected.selectedText()); return;
        }
    }
    const auto prefix=wiki_prefix(); emit wiki_query_changed(prefix?*prefix:QString{},bool(prefix));
}

void PageEditor::insertFromMimeData(const QMimeData* source)
{
    if (source->hasFormat(ClipboardFormat) || source->hasHtml()) {
        Page fragment;
        try {
            fragment=source->hasFormat(ClipboardFormat)?decode_document(source->data(ClipboardFormat).toStdString()):html_fragment(source->html());
        } catch (const std::exception& e) { emit paste_rejected(QString::fromUtf8(e.what())); return; }
        mutate([&] {
            const auto before=snapshot(); auto cursor=textCursor(); cursor.removeSelectedText();
            const auto destination=cursor.blockFormat().stringProperty(Id).toStdString();
            const auto parent=cursor.blockFormat().stringProperty(ParentId).toStdString();
            const auto level=cursor.blockFormat().indent();
            auto tail_cursor=cursor; tail_cursor.movePosition(QTextCursor::EndOfBlock,QTextCursor::KeepAnchor);
            const QTextDocumentFragment tail(tail_cursor); tail_cursor.removeSelectedText();
            const bool separate=fragment.blocks[0].kind==BlockKind::Divider && !cursor.block().text().isEmpty();
            std::unordered_map<std::string,std::string> remap;
            for (std::size_t i=0;i<fragment.blocks.size();++i) remap[fragment.blocks[i].id]=(i==0 && !separate)?destination:new_id().toStdString();
            std::unordered_map<std::string,int> levels; std::string continuation=destination;
            for (std::size_t i=0;i<fragment.blocks.size();++i) {
                auto block=fragment.blocks[i]; block.id=remap.at(block.id);
                block.parent_id=remap.count(block.parent_id)?remap.at(block.parent_id):parent;
                for (auto& run:block.runs) if (run.reference && run.reference->kind==ReferenceKind::Block && remap.count(run.reference->target_id))
                    run.reference->target_id=remap.at(run.reference->target_id);
                const auto nesting=block.parent_id==parent?level:levels.at(block.parent_id)+1; levels[block.id]=nesting;
                if (i || separate) cursor.insertBlock(QTextBlockFormat{},QTextCharFormat{});
                if (i || separate || cursor.block().text().isEmpty()) apply_block_format(cursor,block,nesting);
                if (block.parent_id==parent) continuation=block.id;
                for (const auto& run:block.runs) cursor.insertText(run.asset?QString(QChar::ObjectReplacementCharacter):QString::fromStdString(run.text),run_format(run));
            }
            if (cursor.blockFormat().intProperty(Kind)==int(BlockKind::Divider) && !tail.isEmpty()) {
                const auto nesting=cursor.blockFormat().indent();
                Block after{new_id().toStdString(),cursor.blockFormat().stringProperty(ParentId).toStdString(),BlockKind::Paragraph,{}};
                cursor.insertBlock(); apply_block_format(cursor,after,nesting);
                if (after.parent_id==parent) continuation=after.id;
            }
            const auto end=cursor.position(); const auto last_format=cursor.blockFormat();
            cursor.insertFragment(tail); cursor.setBlockFormat(last_format);
            cursor.setPosition(end); cursor.setCharFormat(QTextCharFormat{}); setTextCursor(cursor);
            // Match Enter: old children follow the trailing root after a paste.
            if (continuation!=destination) {
                std::unordered_set<std::string> children;
                for (const auto& block:before.blocks) if (block.parent_id==destination) children.insert(block.id);
                for (auto qt=document()->begin();qt.isValid();qt=qt.next()) if (children.count(qt.blockFormat().stringProperty(Id).toStdString())) {
                    auto format=qt.blockFormat(); format.setProperty(ParentId,QString::fromStdString(continuation)); QTextCursor child(qt); child.setBlockFormat(format);
                }
            }
        });
        return;
    }
    if (!source->hasText())
        return;
    mutate([&] {
        auto cursor = textCursor();
        cursor.removeSelectedText();
        auto text = source->text();
        text.replace("\r\n", "\n");
        text.replace('\r', '\n');
        const auto paragraphs = text.split('\n');
        for (qsizetype i = 0; i < paragraphs.size(); ++i) {
            if (i)
                split_block(cursor);
            cursor.insertText(paragraphs[i], QTextCharFormat{});
        }
        setTextCursor(cursor);
    });
}

QMimeData* PageEditor::createMimeDataFromSelection() const
{
    // Qt's lazy text MIME object has its own formats() implementation. Copy its
    // materialized formats into an ordinary QMimeData before adding our payload.
    std::unique_ptr<QMimeData> native(QTextEdit::createMimeDataFromSelection());
    auto* mime=new QMimeData;
    for (const auto& format:native->formats()) mime->setData(format,native->data(format));
    const auto cursor=textCursor();
    if (!cursor.hasSelection()) return mime;
    Page fragment{page_id_,{}}; std::unordered_set<std::string> selected;
    QStringList plain;
    for (auto qt=document()->begin();qt.isValid() && qt.position()<=cursor.selectionEnd();qt=qt.next()) {
        if (qt.position()+qt.length()<=cursor.selectionStart()) continue;
        auto block=read_block(qt,cursor.selectionStart(),cursor.selectionEnd());
        if (!selected.count(block.parent_id)) block.parent_id.clear();
        selected.insert(block.id);
        if ((block.kind==BlockKind::Image || block.kind==BlockKind::Attachment) && block.runs.empty()) { block.kind=BlockKind::Paragraph; block.asset_label.clear(); }
        QString text=block.kind==BlockKind::Divider?"---":QString{};
        for (const auto& run:block.runs) text+=QString::fromStdString(run.asset?run.asset->label:run.text);
        plain.push_back(text.replace(QChar::LineSeparator,'\n')); fragment.blocks.push_back(std::move(block));
    }
    mime->setData(ClipboardFormat,QByteArray::fromStdString(encode_document(fragment)));
    mime->setText(plain.join('\n')); return mime;
}

void PageEditor::cut() { mutate([&] { QTextEdit::cut(); }); }
void PageEditor::paste() { insertFromMimeData(QApplication::clipboard()->mimeData()); }

bool PageEditor::move_current(int direction)
{
    auto page = snapshot();
    const auto id = current_block_id();
    if (!move_subtree(page, id.toStdString(), direction))
        return false;
    write_page(page, id);
    return true;
}

bool PageEditor::indent_current()
{
    auto page = snapshot();
    const auto id = current_block_id();
    if (!indent(page, id.toStdString()))
        return false;
    write_page(page, id);
    return true;
}

bool PageEditor::outdent_current()
{
    auto page = snapshot();
    const auto id = current_block_id();
    if (!outdent(page, id.toStdString()))
        return false;
    write_page(page, id);
    return true;
}

void PageEditor::set_block_kind(BlockKind kind)
{
    auto page = snapshot();
    const auto id = current_block_id();
    for (auto& block : page.blocks) {
        if (block.id == id.toStdString()) {
            block.kind = kind;
            if (kind != BlockKind::Image && kind != BlockKind::Attachment)
                block.asset_label.clear();
        }
    }
    write_page(page, id);
}

void PageEditor::toggle_checked()
{
    mutate([&] {
        auto cursor = textCursor();
        auto format = cursor.blockFormat();
        if (format.intProperty(Kind) != static_cast<int>(BlockKind::Todo))
            return;
        const bool checked = !format.boolProperty(Checked);
        format.setProperty(Checked, checked);
        format.setMarker(checked ? QTextBlockFormat::MarkerType::Checked : QTextBlockFormat::MarkerType::Unchecked);
        cursor.setBlockFormat(format);
    });
}

void PageEditor::insert_reference(const QString& label, ReferenceKind kind, const QString& target_id)
{
    if (label.isEmpty() || target_id.isEmpty())
        return;
    mutate([&] {
        auto cursor = textCursor();
        Run run;
        run.reference = Reference{kind, target_id.toStdString()};
        cursor.insertText(label, run_format(run));
        cursor.setCharFormat(QTextCharFormat{});
        setTextCursor(cursor);
    });
}

void PageEditor::insert_card(bool image, const QString& label)
{
    if (label.isEmpty())
        return;
    mutate([&] {
        auto cursor = textCursor();
        cursor.movePosition(QTextCursor::EndOfBlock);
        split_block(cursor);
        auto format = cursor.blockFormat();
        format.setProperty(Kind, static_cast<int>(image ? BlockKind::Image : BlockKind::Attachment));
        format.setProperty(AssetLabel, label);
        cursor.setBlockFormat(format);
        Run run;
        run.asset = Asset{image, label.toStdString()};
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), run_format(run));
        cursor.setCharFormat(QTextCharFormat{});
        setTextCursor(cursor);
    });
}

void PageEditor::insert_divider()
{
    auto page=snapshot(); const auto current=current_block_id().toStdString();
    auto here=std::find_if(page.blocks.begin(),page.blocks.end(),[&](const auto& block){return block.id==current;});
    if (here==page.blocks.end()) return;
    const auto parent=here->parent_id;
    std::unordered_set<std::string> descendants{current}; auto end=here+1;
    while (end!=page.blocks.end() && descendants.count(end->parent_id)) { descendants.insert(end->id); ++end; }
    const auto following=new_id();
    page.blocks.insert(end,{{new_id().toStdString(),parent,BlockKind::Divider,{}},
                            {following.toStdString(),parent,BlockKind::Paragraph,{}}});
    write_page(page,following);
}

void PageEditor::toggle_bold()
{
    mutate([&] {
        QTextCharFormat format;
        format.setFontWeight(currentCharFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        mergeCurrentCharFormat(format);
    });
}

void PageEditor::insert_unresolved_reference(const QString& label, const QString& name, const QString& scope)
{
    if (name.isEmpty()) return;
    mutate([&] {
        auto cursor=textCursor();
        Run run{label.toStdString(),false,false,Reference{ReferenceKind::Page,"",name.toStdString(),scope.toStdString()}};
        cursor.insertText(label,run_format(run)); cursor.setCharFormat(QTextCharFormat{}); setTextCursor(cursor);
    });
}

void PageEditor::toggle_italic()
{
    mutate([&] {
        QTextCharFormat format;
        format.setFontItalic(!currentCharFormat().fontItalic());
        mergeCurrentCharFormat(format);
    });
}

void PageEditor::cancel_drag()
{
    grip_pressed_=false; dragging_=false; drop_valid_=false;
    drag_scroll_->stop(); viewport()->setCursor(Qt::IBeamCursor); viewport()->update();
}

void PageEditor::mousePressEvent(QMouseEvent* event)
{
    typing_position_=-1;
    if (event->button()==Qt::LeftButton && !isReadOnly() && grip_rect().contains(event->position())) {
        grip_pressed_=true; dragging_=false; drop_valid_=false;
        dragged_id_=current_block_id(); drag_start_=event->position().toPoint(); setFocus(); event->accept(); return;
    }
    QTextEdit::mousePressEvent(event);
}

void PageEditor::focusOutEvent(QFocusEvent* event)
{
    typing_position_=-1;
    QTextEdit::focusOutEvent(event);
}

void PageEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (grip_pressed_) {
        drag_position_=event->position().toPoint();
        if (!dragging_ && (drag_position_-drag_start_).manhattanLength()>=QApplication::startDragDistance()) {
            dragging_=true; drag_scroll_->start(); viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (dragging_) update_drop();
        event->accept(); return;
    }
    viewport()->setCursor(grip_rect().contains(event->position())?Qt::OpenHandCursor:Qt::IBeamCursor);
    QTextEdit::mouseMoveEvent(event);
}

void PageEditor::update_drop()
{
    drop_valid_=false;
    if (!viewport()->rect().contains(drag_position_)) { viewport()->update(); return; }
    const auto cursor=cursorForPosition(drag_position_);
    const auto target_id=cursor.blockFormat().stringProperty(Id).toStdString();
    auto page=snapshot();
    const auto target=std::find_if(page.blocks.begin(),page.blocks.end(),[&](const Block& b){return b.id==target_id;});
    if (target==page.blocks.end()) return;
    const auto rect=block_rect(cursor.block());
    const qreal text_left=cursorRect(QTextCursor(cursor.block())).left();
    std::string parent=target->parent_id, before=target_id;
    drop_marker_={rect.left()+20,rect.top()};
    const bool nest=drag_position_.x()>text_left+24;
    if (nest || drag_position_.y()>rect.center().y()) {
        std::unordered_set<std::string> descendants{target_id};
        auto end=target+1;
        while (end!=page.blocks.end() && descendants.count(end->parent_id)) { descendants.insert(end->id); ++end; }
        auto last=cursor.block();
        for (auto qt=last.next();qt.isValid() && descendants.count(qt.blockFormat().stringProperty(Id).toStdString());qt=qt.next()) last=qt;
        drop_marker_.setY(block_rect(last).bottom());
        before=(end!=page.blocks.end() && end->parent_id==parent)?end->id:std::string{};
        if (nest) { parent=target_id; before.clear(); drop_marker_.setX(text_left+32); }
    }
    try { drop_valid_=relocate_subtree(page,dragged_id_.toStdString(),before,parent); drop_preview_=std::move(page); }
    catch (const std::invalid_argument&) { drop_valid_=false; }
    viewport()->update();
}

void PageEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button()==Qt::LeftButton && grip_pressed_) {
        if (dragging_) { drag_position_=event->position().toPoint(); update_drop(); }
        const bool apply=dragging_ && drop_valid_;
        const auto page=drop_preview_; const auto focus=dragged_id_;
        cancel_drag();
        if (apply) write_page(page,focus);
        event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier) {
        auto cursor = cursorForPosition(event->position().toPoint());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const auto format = cursor.charFormat();
        if (format.hasProperty(RefTarget)) {
            setTextCursor(cursorForPosition(event->position().toPoint())); activate_current_reference();
            event->accept();
            return;
        }
    }
    QTextEdit::mouseReleaseEvent(event);
}

void PageEditor::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    menu.addAction("Undo", this, &QTextEdit::undo);
    menu.addAction("Redo", this, &QTextEdit::redo);
    menu.addSeparator();
    menu.addAction("Cut", this, &PageEditor::cut);
    menu.addAction("Copy", this, &QTextEdit::copy);
    menu.addAction("Paste plain text", this, &PageEditor::paste);
    menu.addAction("Select all", this, &QTextEdit::selectAll);
    menu.exec(event->globalPos());
}

} // namespace nebula

#include "page_editor.moc"
