#pragma once

#include "nebula/core/document.hpp"
#include "nebula/core/position.hpp"

#include <QElapsedTimer>
#include <QTextEdit>
#include <functional>
#include <map>
#include <unordered_set>

class QTimer;
class QCompleter;

namespace nebula {

class CardRenderer;

// Toolbar state derived from the active block and its neighbours, without
// converting the whole document.
struct BlockState {
    std::string id;
    int index = 0, count = 0, depth = 0;
    bool up = false, down = false, indent = false, outdent = false;
};

// Page adapter with application-owned snapshots. QTextDocument supplies native
// selection, layout and undo; it is not the persistent representation.
class PageEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit PageEditor(QWidget* parent = nullptr);
    void set_page(const Page& page, const QString& title = {});
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
    void insert_reference(const QString& label, ReferenceKind kind, const QString& target_id, bool custom_label = true);
    void insert_unresolved_reference(const QString& label, const QString& name, const QString& database_scope,
                                     bool custom_label = true);
    void set_reference_label_resolver(std::function<std::optional<std::string>(const Reference&)> resolver);
    QString page_title() const;
    void set_page_title(const QString& title);
    bool select_current_reference();
    bool select_reference_at(const ReferenceLocation& location);
    std::optional<ReferenceLocation> selected_reference_location() const;
    void replace_selected_reference(const Reference& reference, const QString& label);
    void set_attachment_resolver(std::function<std::optional<std::string>(const std::string&)> resolver);
    void apply_document(const Page& page, const QString& focus = {});
    std::string current_attachment_id() const;
    void insert_card(bool image, const QString& label);
    void insert_divider();
    void toggle_bold();
    void toggle_italic();
    void cut();
    void paste();
    // Inserts only text/plain clipboard content (Ctrl+Shift+V).
    void paste_plain_text();
    BlockState block_state() const;
    // Blocks merged into a neighbour by Backspace/Delete since the last call.
    // Their text survives, so storage does not archive them as deletions.
    std::unordered_set<std::string> take_absorbed_blocks();
    void set_wiki_completer(QCompleter* completer);
    std::optional<QString> wiki_prefix() const;
    bool select_wiki_link();
    EditorPosition capture_position() const;
    bool restore_position(const EditorPosition& position);
    std::optional<Reference> current_reference() const;
    bool activate_current_reference();

signals:
    void reference_activated(int kind, const QString& target_id);
    void unresolved_reference_activated(const QString& name, const QString& database_scope);
    void paste_rejected(const QString& reason);
    void web_link_pasted(const QString& url, const QString& source_block);
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
    void resizeEvent(QResizeEvent* event) override;

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
    QString fallback_title_;
    std::function<std::optional<std::string>(const Reference&)> label_resolver_;
    std::function<std::optional<std::string>(const std::string&)> attachment_resolver_;
    std::map<std::pair<int, std::string>, std::optional<std::string>> external_labels_;
    std::unordered_set<std::string> default_targets_;
    std::unordered_set<std::string> absorbed_;
    QRectF painted_active_;
    CardRenderer* cards_ = nullptr;
    void refresh_reference_labels(const Page* before = nullptr, const QString& before_title = {});
    bool fast_typing_allowed(const QKeyEvent* event) const;
    void update_active_region();
    // Rewrites only the blocks that differ between the document and target,
    // keeping the caret's block, offset and anchor where they still exist.
    void apply_structure(const Page& target, const QString& focus_id = {});
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
