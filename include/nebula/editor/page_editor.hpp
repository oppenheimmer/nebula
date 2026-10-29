#pragma once

#include "nebula/core/document.hpp"

#include <QTextEdit>
#include <functional>

class QTimer;

namespace nebula {

// Investigation adapter. Its snapshots are application-owned core values;
// QTextDocument supplies the prototype's selection, layout and undo engine.
class PageEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit PageEditor(QWidget* parent = nullptr);
    void set_page(const Page& page);
    Page snapshot() const;
    QString current_block_id() const;
    QRectF active_block_rect() const; // Viewport coordinates, including wrapped lines.
    QRectF grip_rect() const;
    bool go_to_block(const QString& id);
    bool move_current(int direction);
    bool indent_current();
    bool outdent_current();
    void set_block_kind(BlockKind kind);
    void toggle_checked();
    void insert_reference(const QString& label, ReferenceKind kind, const QString& target_id);
    void insert_card(bool image, const QString& label);
    void toggle_bold();
    void toggle_italic();
    void cut();
    void paste();

signals:
    void reference_activated(int kind, const QString& target_id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    std::string page_id_;
    bool grip_pressed_ = false;
    bool dragging_ = false;
    bool drop_valid_ = false;
    QPoint drag_start_, drag_position_;
    QString dragged_id_;
    Page drop_preview_;
    QPointF drop_marker_;
    QTimer* drag_scroll_;
    QRectF block_rect(const QTextBlock& block) const;
    void update_drop();
    void cancel_drag();
    void mutate(const std::function<void()>& action);
    void normalize(const Page& before);
    void write_page(const Page& page, const QString& focus_id = {});
    void split_block(QTextCursor& cursor);
};

} // namespace nebula
