#pragma once

#include "nebula/core/document.hpp"

#include <QTextEdit>
#include <QElapsedTimer>
#include <functional>

class QTimer;
class QCompleter;

namespace nebula {

// Page adapter with application-owned snapshots. QTextDocument supplies native
// selection, layout and undo; it is not the persistent representation.
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
    void insert_unresolved_reference(const QString& label, const QString& name, const QString& database_scope);
    void insert_card(bool image, const QString& label);
    void insert_divider();
    void toggle_bold();
    void toggle_italic();
    void cut();
    void paste();
    void set_wiki_completer(QCompleter* completer);
    std::optional<QString> wiki_prefix() const;
    bool select_wiki_link();

signals:
    void reference_activated(int kind, const QString& target_id);
    void unresolved_reference_activated(const QString& name, const QString& database_scope);
    void paste_rejected(const QString& reason);
    void wiki_query_changed(const QString& query, bool active);
    void wiki_link_requested(const QString& syntax);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    QMimeData* createMimeDataFromSelection() const override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

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
    QElapsedTimer typing_clock_;
    int typing_position_ = -1;
    int typing_revision_ = -1;
    QCompleter* wiki_completer_ = nullptr;
    int wiki_start() const;
    void process_wiki_input();
    QRectF block_rect(const QTextBlock& block) const;
    void update_drop();
    void cancel_drag();
    void mutate(const std::function<void()>& action, bool join_typing = false);
    void normalize(const Page& before);
    void write_page(const Page& page, const QString& focus_id = {});
    void split_block(QTextCursor& cursor, bool exit_empty_list = false);
};

} // namespace nebula
