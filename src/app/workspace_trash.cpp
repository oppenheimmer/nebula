#include "nebula/app/workspace_window.hpp"
#include "nebula/app/trash_controls.hpp"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QStatusBar>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <unordered_set>

namespace nebula {
namespace {
QString q(const std::string& text) {return QString::fromUtf8(text.data(),qsizetype(text.size()));}
}
void WorkspaceWindow::update_trash_actions()
{
    const bool page=session_.has_page() && page_editor()->isEnabled();
    for(const auto* name:{"trashPage","trashBlock"}) if(auto* action=findChild<QAction*>(name)) action->setEnabled(page);
    if(auto* action=findChild<QAction*>("trashDatabase")) action->setEnabled(!database_id_.empty());
}
void WorkspaceWindow::trash_current_database()
{
    if(database_id_.empty()) return;
    const auto id=database_id_; const auto name=databases_->currentText();
    const auto count=workspace_.pages(id,true).size();
    autosave_->stop();
    const bool confirmed=confirm_trash(this,TrashScope::Database,name,q(id),count);
    activateWindow(); page_editor()->setFocus();
    if(!confirmed) {if(pending_) autosave_->start(); return;}
    if(!flush()) return;
    workspace_.trash_database(id,true); session_.close(); refresh_databases();
    statusBar()->showMessage("Database moved to Trash. Use Restore to recover it.",8000);
}
void WorkspaceWindow::trash_current_page()
{
    if(!session_.has_page()) return;
    const auto id=session_.record().document.id; const auto count=page_editor()->snapshot().blocks.size();
    autosave_->stop();
    const bool confirmed=confirm_trash(this,TrashScope::Page,title_->text(),q(id),count);
    activateWindow(); page_editor()->setFocus();
    if(!confirmed) {if(pending_) autosave_->start(); return;}
    if(!flush()) return;
    workspace_.trash_page(id,true); session_.close(); refresh_databases();
    statusBar()->showMessage("Page moved to Trash. Use Restore to recover it.",8000);
}
void WorkspaceWindow::trash_current_block()
{
    if(!session_.has_page()) return;
    auto page=page_editor()->snapshot(); const auto id=page_editor()->current_block_id().toStdString();
    const auto first=std::find_if(page.blocks.begin(),page.blocks.end(),[&](const auto& block){return block.id==id;});
    if(first==page.blocks.end()) return;
    std::unordered_set<std::string> descendants{id}; auto end=first+1;
    while(end!=page.blocks.end() && descendants.count(end->parent_id)) {descendants.insert(end->id); ++end;}
    const auto index=std::size_t(first-page.blocks.begin()); const auto count=std::size_t(end-first);
    auto label=reference_title(*first); if(label.empty()) label=first->asset_label;
    autosave_->stop();
    const bool confirmed=confirm_trash(this,TrashScope::Block,q(label),q(id),count);
    activateWindow(); page_editor()->setFocus();
    if(!confirmed) {if(pending_) autosave_->start(); return;}
    if(!flush()) return;
    page.blocks.erase(page.blocks.begin()+index,page.blocks.begin()+index+count);
    if(page.blocks.empty()) page.blocks.push_back({QUuid::createUuid().toString(QUuid::Id128).toStdString()});
    const auto focus=q(page.blocks[std::min(index,page.blocks.size()-1)].id);
    page_editor()->apply_document(page,focus); // One native undo group; save archives the removed subtree.
    if(!flush()) {
        const auto failure=save_state_->text();
        loading_=true; page_editor()->undo(); loading_=false;
        pending_=true; autosave_->stop(); save_state_->setText(failure); update_trash_actions(); return;
    }
    statusBar()->showMessage("Block moved to Trash. Undo here, or use Restore after reopening.",8000);
}
} // namespace nebula
