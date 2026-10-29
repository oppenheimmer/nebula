#include "nebula/app/prototype_window.hpp"
#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace nebula {

PrototypeWindow::PrototypeWindow(const Page& page, const QString& workspace,
                                 bool show_comparison, QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("Nebula — editor prototype");
    resize(1100, 850);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    if (!workspace.isEmpty()) {
        auto* location = new QLabel("Workspace: " + workspace, central);
        location->setObjectName("workspaceLocation");
        location->setTextFormat(Qt::PlainText);
        location->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(location);
    }
    auto* notice = new QLabel("EDITOR PROTOTYPE · Sample content stays in memory and is discarded on close.", central);
    notice->setStyleSheet("QLabel { padding: 10px; background: #fff3d5; color: #654c13; }");
    layout->addWidget(notice);
    selection_ = new QLabel(central);
    selection_->setObjectName("activeBlock");
    layout->addWidget(selection_);
    editor_ = new PageEditor(central);
    editor_->set_page(page);
    BlockWidgets* comparison = nullptr;
    QTabWidget* tabs = nullptr;
    if (show_comparison) {
        tabs = new QTabWidget(central);
        comparison = new BlockWidgets(tabs);
        comparison->set_page(page);
        tabs->addTab(editor_, "Block editor");
        tabs->addTab(comparison, "Investigation only: separate text widgets");
        layout->addWidget(tabs);
    } else {
        layout->addWidget(editor_);
    }
    setCentralWidget(central);

    auto* formatting = addToolBar("Text formatting");
    formatting->setMovable(false);
    auto add_action = [this](QToolBar* bar, const QString& text, const QString& name,
                             const std::function<void()>& callback) {
        auto* action = bar->addAction(text);
        action->setObjectName(name);
        editor_->addAction(action);
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QAction::triggered, this, [this, callback] {
            callback();
            editor_->setFocus(Qt::OtherFocusReason);
            queue_refresh();
        });
        return action;
    };
    add_action(formatting, "Undo", "undo", [this] { editor_->undo(); })->setShortcut(QKeySequence::Undo);
    add_action(formatting, "Redo", "redo", [this] { editor_->redo(); })->setShortcut(QKeySequence::Redo);
    formatting->addSeparator();
    add_action(formatting, "Bold", "bold", [this] { editor_->toggle_bold(); })->setShortcut(QKeySequence::Bold);
    add_action(formatting, "Italic", "italic", [this] { editor_->toggle_italic(); })->setShortcut(QKeySequence::Italic);
    for (const auto& entry : {std::pair{"Paragraph", BlockKind::Paragraph}, {"Heading", BlockKind::Heading},
                              {"Bullet", BlockKind::Bullet}, {"To-do", BlockKind::Todo}}) {
        const auto kind = entry.second;
        add_action(formatting, entry.first, entry.first, [this, kind] { editor_->set_block_kind(kind); });
    }
    add_action(formatting, "Check", "check", [this] { editor_->toggle_checked(); });
    addToolBarBreak();
    auto* blocks = addToolBar("Block controls");
    blocks->setObjectName("blockControls");
    blocks->setMovable(false);
    move_up_ = add_action(blocks, "Move up", "moveUp", [this] { editor_->move_current(-1); });
    move_up_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    move_down_ = add_action(blocks, "Move down", "moveDown", [this] { editor_->move_current(1); });
    move_down_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    indent_ = add_action(blocks, "Indent", "indent", [this] { editor_->indent_current(); });
    indent_->setToolTip("Nest under the previous sibling (Tab). The first sibling cannot be indented.");
    outdent_ = add_action(blocks, "Outdent", "outdent", [this] { editor_->outdent_current(); });
    outdent_->setToolTip("Move out of the current parent (Shift+Tab).");
    blocks->addSeparator();
    add_action(blocks, "Link to heading", "link", [this] {
        editor_->insert_reference("the heading", ReferenceKind::Block, "heading");
    });
    add_action(blocks, "Image card", "image", [this] { editor_->insert_card(true, "Sample image representation"); });
    add_action(blocks, "File card", "attachment", [this] { editor_->insert_card(false, "example.txt"); });
    auto* fixtures = menuBar()->addMenu("Sample");
    connect(fixtures->addAction("Reset sample"), &QAction::triggered, this, [this, comparison] {
        editor_->set_page(sample_page());
        if (comparison)
            comparison->set_page(sample_page());
        editor_->setFocus();
    });
    connect(fixtures->addAction("Load 1,000 blocks"), &QAction::triggered, this, [this, comparison] {
        editor_->set_page(long_page(1000));
        if (comparison)
            comparison->set_page(long_page(1000));
        editor_->setFocus();
    });
    if (tabs) {
        connect(tabs, &QTabWidget::currentChanged, this, [this, formatting, blocks](int index) {
            comparison_active_ = index != 0;
            formatting->setEnabled(!comparison_active_);
            blocks->setEnabled(!comparison_active_);
            queue_refresh();
        });
    }
    connect(editor_, &PageEditor::cursorPositionChanged, this, &PrototypeWindow::queue_refresh);
    connect(editor_, &PageEditor::textChanged, this, &PrototypeWindow::queue_refresh);
    connect(editor_, &PageEditor::reference_activated, this, [this](int kind, const QString& id) {
        if (kind == static_cast<int>(ReferenceKind::Block) && editor_->go_to_block(id))
            statusBar()->showMessage("Followed reference", 4000);
        else
            statusBar()->showMessage("This reference target is outside the sample page", 4000);
    });
    statusBar()->showMessage("Click a block to select it · Alt+↑/↓: move · Tab/Shift+Tab: nest · Enter: new block");
    refresh_selection();
    editor_->setFocus();
}

void PrototypeWindow::queue_refresh()
{
    if (refresh_pending_)
        return;
    refresh_pending_ = true;
    QTimer::singleShot(0, this, [this] { refresh_pending_ = false; refresh_selection(); });
}

void PrototypeWindow::refresh_selection()
{
    auto page = editor_->snapshot();
    const auto id = editor_->current_block_id().toStdString();
    const auto found = std::find_if(page.blocks.begin(), page.blocks.end(),
                                    [&](const Block& block) { return block.id == id; });
    if (found == page.blocks.end())
        return;
    if (comparison_active_) {
        selection_->setText("Comparison mode: each text widget has its own selection and undo. Use the Block editor tab to move or nest blocks.");
    } else {
        selection_->setText(QString("Block %1 of %2 · Nesting level %3 · Select a later sibling to indent it.")
            .arg(found - page.blocks.begin() + 1).arg(page.blocks.size()).arg(nebula::depth(page, id)));
    }
    const auto possible = [&](auto operation) { auto copy = page; return operation(copy); };
    move_up_->setEnabled(!comparison_active_ && possible([&](Page& copy) { return move_subtree(copy, id, -1); }));
    move_down_->setEnabled(!comparison_active_ && possible([&](Page& copy) { return move_subtree(copy, id, 1); }));
    indent_->setEnabled(!comparison_active_ && possible([&](Page& copy) { return indent(copy, id); }));
    outdent_->setEnabled(!comparison_active_ && possible([&](Page& copy) { return outdent(copy, id); }));
}

} // namespace nebula
