#pragma once
#include "nebula/app/prototype_window.hpp"
#include "nebula/app/page_session.hpp"

class QComboBox;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTimer;
class QCloseEvent;
class QCompleter;
class QStandardItemModel;

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
    std::optional<BlockTransfer> last_transfer_;
    std::string transferred_block_;
    struct LinkCandidate {
        ReferenceKind kind;
        std::string id, label, page, title, database;
        bool heading = false;
    };
    QCompleter* link_completer_;
    QStandardItemModel* completion_model_;
    std::vector<LinkCandidate> completion_targets_;
    bool completing_ = false;
    std::vector<LinkCandidate> link_candidates() const;
    void complete_wiki(const QString& prefix, bool active);
    void enter_wiki_link(const QString& syntax);
    void switch_database();
    void mark_pending();
    void update_links();
    void add_property();
    void edit_view(bool create);
    void show_trash();
    void insert_link(const QString& query = {}, const QString& label = {});
    void follow_reference(ReferenceKind kind, const std::string& id);
    void move_page();
    void move_block();
    void undo_block_transfer(bool applied);
    void reload_page(const std::string& block = {});
    void update_transfer_actions();
};
} // namespace nebula
