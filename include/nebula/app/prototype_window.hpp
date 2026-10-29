#pragma once

#include "nebula/core/document.hpp"
#include <QMainWindow>

class QAction;
class QLabel;

namespace nebula {

class PageEditor;

class PrototypeWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PrototypeWindow(const Page& page = sample_page(), const QString& workspace = {},
                             bool show_comparison = false, QWidget* parent = nullptr);
    PageEditor* page_editor() const { return editor_; }

private:
    PageEditor* editor_;
    QLabel* selection_;
    QAction* move_up_;
    QAction* move_down_;
    QAction* indent_;
    QAction* outdent_;
    bool refresh_pending_ = false;
    bool comparison_active_ = false;
    void queue_refresh();
    void refresh_selection();
};

} // namespace nebula
