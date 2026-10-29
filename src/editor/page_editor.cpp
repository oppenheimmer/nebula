#include "nebula/editor/page_editor.hpp"

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
#include <QTextList>
#include <QTextObjectInterface>
#include <QUuid>
#include <QScrollBar>
#include <QTimer>
#include <QApplication>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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
constexpr int CardObject = QTextFormat::UserObject + 1;

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
        format.setForeground(QColor("#236b9a"));
        format.setFontUnderline(true);
    }
    if (run.asset) {
        format.setObjectType(CardObject);
        format.setProperty(CardImage, run.asset->image);
        format.setProperty(AssetLabel, QString::fromStdString(run.asset->label));
    }
    return format;
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
    cancel_drag();
    validate(page);
    page_id_ = page.id;
    write_page(page);
    document()->clearUndoRedoStacks();
    document()->setModified(false);
}

void PageEditor::write_page(const Page& page, const QString& focus_id)
{
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
    for (auto qt_block = document()->begin(); qt_block.isValid(); qt_block = qt_block.next()) {
        const auto format = qt_block.blockFormat();
        Block block;
        block.id = format.stringProperty(Id).toStdString();
        block.parent_id = format.stringProperty(ParentId).toStdString();
        block.kind = static_cast<BlockKind>(format.intProperty(Kind));
        block.asset_label = format.stringProperty(AssetLabel).toStdString();
        block.checked = format.boolProperty(Checked);
        for (auto it = qt_block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().isEmpty())
                continue;
            const auto character = fragment.charFormat();
            Run run;
            run.text = fragment.text().toStdString();
            run.bold = character.fontWeight() >= QFont::Bold;
            run.italic = character.fontItalic();
            if (character.hasProperty(RefTarget))
                run.reference = Reference{static_cast<ReferenceKind>(character.intProperty(RefKind)),
                                          character.stringProperty(RefTarget).toStdString()};
            if (character.objectType() == CardObject)
                run.asset = Asset{character.boolProperty(CardImage),
                                  character.stringProperty(AssetLabel).toStdString()};
            block.runs.push_back(std::move(run));
        }
        page.blocks.push_back(std::move(block));
    }
    return page;
}

QString PageEditor::current_block_id() const { return textCursor().blockFormat().stringProperty(Id); }

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

void PageEditor::mutate(const std::function<void()>& action)
{
    const auto before = snapshot();
    QTextCursor group = textCursor();
    group.beginEditBlock();
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

void PageEditor::split_block(QTextCursor& cursor)
{
    cursor.removeSelectedText();
    auto format = cursor.blockFormat();
    const auto original_id = format.stringProperty(Id).toStdString();
    const auto trailing_id = new_id();
    const auto original_kind = static_cast<BlockKind>(format.intProperty(Kind));
    const bool list = original_kind == BlockKind::Bullet || original_kind == BlockKind::Todo;
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
    if (grip_pressed_) {
        if (event->key()==Qt::Key_Escape) cancel_drag();
        event->accept(); return;
    }
    const auto modifiers = event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        QTextEdit::keyPressEvent(event);
        return;
    }
    if (modifiers == Qt::AltModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        move_current(event->key() == Qt::Key_Up ? -1 : 1);
        return;
    }
    if (event->key() == Qt::Key_Tab && modifiers == Qt::NoModifier) {
        indent_current();
        return;
    }
    if (event->key() == Qt::Key_Backtab || (event->key() == Qt::Key_Tab && modifiers == Qt::ShiftModifier)) {
        outdent_current();
        return;
    }
    mutate([&] {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            auto cursor = textCursor();
            if (event->modifiers() & Qt::ShiftModifier)
                cursor.insertText(QString(QChar::LineSeparator));
            else
                split_block(cursor);
            setTextCursor(cursor);
        } else {
            QTextEdit::keyPressEvent(event);
        }
    });
}

void PageEditor::inputMethodEvent(QInputMethodEvent* event)
{
    mutate([&] { QTextEdit::inputMethodEvent(event); });
}

void PageEditor::insertFromMimeData(const QMimeData* source)
{
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

void PageEditor::toggle_bold()
{
    mutate([&] {
        QTextCharFormat format;
        format.setFontWeight(currentCharFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        mergeCurrentCharFormat(format);
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
    if (event->button()==Qt::LeftButton && !isReadOnly() && grip_rect().contains(event->position())) {
        grip_pressed_=true; dragging_=false; drop_valid_=false;
        dragged_id_=current_block_id(); drag_start_=event->position().toPoint(); setFocus(); event->accept(); return;
    }
    QTextEdit::mousePressEvent(event);
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
            emit reference_activated(format.intProperty(RefKind), format.stringProperty(RefTarget));
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
