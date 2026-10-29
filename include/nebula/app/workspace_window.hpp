#pragma once
#include "nebula/app/prototype_window.hpp"
#include "nebula/app/page_session.hpp"

class QComboBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTimer;
class QCloseEvent;

namespace nebula {
class WorkspaceWindow : public PrototypeWindow {
    Q_OBJECT
public:
    explicit WorkspaceWindow(Workspace& workspace, const QString& display_path = {}, QWidget* parent = nullptr);
    bool flush();
    bool open_page(const std::string& id, const std::string& block = {});
    void create_page(const QString& title);
    void refresh_databases();
    void refresh_table();
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    Workspace& workspace_;
    PageSession session_;
    QComboBox* databases_;
    QComboBox* views_;
    QTableWidget* table_;
    QLineEdit* title_;
    QLabel* save_state_;
    QListWidget* backlinks_;
    QTimer* autosave_;
    std::vector<PropertyDefinition> definitions_;
    std::vector<View> saved_views_;
    View current_view_;
    std::string database_id_;
    bool loading_ = false;
    bool pending_ = false;
    void switch_database();
    void mark_pending();
    void update_links();
    void add_property();
    void edit_view(bool create);
    void show_trash();
    void insert_link();
    void follow_reference(ReferenceKind kind, const std::string& id);
};
} // namespace nebula
