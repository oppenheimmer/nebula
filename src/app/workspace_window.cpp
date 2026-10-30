#include "nebula/interchange/interchange.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/app/link_target_tree.hpp"
#include "nebula/app/view_edit.hpp"
#include "nebula/app/interchange.hpp"
#include "nebula/app/pasted_link_preview.hpp"
#include "nebula/app/trash_controls.hpp"
#include "nebula/app/property_text.hpp"
#include "nebula/attachments/files.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/core/wiki.hpp"
#include "nebula/qt/utf8.hpp"
#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QCompleter>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QImageReader>
#include <QSaveFile>
#include <QUrl>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QEventLoop>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <thread>
#include <unistd.h>

namespace nebula {
namespace {
QString q(const std::string& text) { return utf8::decode(text); }
std::string s(const QString& text) { return utf8::encode(text); }
QString display(const PropertyValue& value) { return q(format_property(value)); }
PropertyValue parse_value(PropertyKind kind, const QString& text) { return parse_property(kind,s(text)); }
// Messages often contain names from notes or imported files; never interpret them as HTML.
void notify(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text) {
    QMessageBox box(icon,title,text,QMessageBox::Ok,parent); box.setTextFormat(Qt::PlainText); box.exec();
}
void error(QWidget* parent, const std::exception& e) { notify(parent,QMessageBox::Warning,"Nebula",q(e.what())); }
bool confirm(QWidget* parent, const char* name, const QString& title, const QString& text) {
    QMessageBox box(QMessageBox::Question,title,text,QMessageBox::Yes|QMessageBox::Cancel,parent);
    box.setObjectName(name); box.setTextFormat(Qt::PlainText); box.setDefaultButton(QMessageBox::Cancel);
    return box.exec()==QMessageBox::Yes;
}
// A cell editor is a focused child of the table's viewport.
bool table_editing(const QTableWidget* table) {
    const auto* focus=QApplication::focusWidget();
    return focus && focus!=table && focus!=table->viewport() && table->isAncestorOf(focus);
}
// A safe local name for an opened copy: no separators, controls or leading dot.
QString copy_name(const std::string& name) {
    QString result;
    for (const auto c:q(name)) result+=(c=='/' || c=='\\' || c.category()==QChar::Other_Control || c.category()==QChar::Other_Format)?QChar('_'):c;
    while (result.startsWith('.')) result.remove(0,1);
    result=result.left(120).trimmed();
    return result.isEmpty()?QString("attachment"):result;
}
std::vector<LinkFilter> selected_link_filters(QTableWidget* table)
{
    std::vector<LinkFilter> result;
    for(int row=0;row<table->rowCount();++row) result.push_back({
        LinkRelation(qobject_cast<QComboBox*>(table->cellWidget(row,0))->currentIndex()),
        s(qobject_cast<QComboBox*>(table->cellWidget(row,1))->currentData().toString())});
    validate_link_filters(result); return result;
}
QWidget* link_filter_controls(QWidget* parent,const Workspace& workspace,const std::vector<LinkFilter>& initial)
{
    auto* panel=new QWidget(parent); auto* layout=new QVBoxLayout(panel); layout->setContentsMargins(0,0,0,0);
    layout->addWidget(new QLabel("Reference filters (all must match)",panel));
    auto* table=new QTableWidget(0,3,panel); table->setObjectName("linkFilters"); table->setAccessibleName("Reference filters");
    table->setHorizontalHeaderLabels({"Relation","Selected page","Remove"}); table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table->setMaximumHeight(180); layout->addWidget(table);
    auto* add=new QPushButton("Add reference filter",panel); add->setObjectName("addLinkFilter"); layout->addWidget(add);
    std::vector<std::pair<QString,QString>> choices;
    for(const auto& page:workspace.page_summaries(true))
        choices.push_back({q(page.database_name)+" / "+q(page.title)+" ["+q(page.id.substr(0,8))+"]"+(page.database_trashed || page.trashed?" (trashed)":""),
            q(page.id)});
    auto append=[table,add,choices](const LinkFilter& filter) {
        const auto row=table->rowCount(); table->insertRow(row);
        auto* relation=new QComboBox(table); relation->setAccessibleName("Reference relation");
        relation->addItems({"References selected page","Referenced by selected page"}); relation->setCurrentIndex(int(filter.relation));
        auto* page=new QComboBox(table); page->setAccessibleName("Selected reference page"); page->addItem("Select a page",QString{});
        for(const auto& choice:choices) page->addItem(choice.first,choice.second);
        if(!filter.page_id.empty() && page->findData(q(filter.page_id))<0) page->addItem("Unavailable page ["+q(filter.page_id)+"]",q(filter.page_id));
        page->setCurrentIndex(page->findData(q(filter.page_id))); page->setMinimumWidth(240);
        auto* remove=new QPushButton("Remove",table); remove->setObjectName("removeLinkFilter"); remove->setAccessibleName("Remove reference filter");
        table->setCellWidget(row,0,relation); table->setCellWidget(row,1,page); table->setCellWidget(row,2,remove);
        QObject::connect(relation,&QComboBox::currentIndexChanged,table,[table]{emit table->cellChanged(0,0);});
        QObject::connect(page,&QComboBox::currentIndexChanged,table,[table]{emit table->cellChanged(0,1);});
        QObject::connect(remove,&QPushButton::clicked,table,[table,add,remove]{
            for(int row=0;row<table->rowCount();++row) if(table->cellWidget(row,2)==remove){table->removeRow(row); break;}
            add->setEnabled(table->rowCount()<8); emit table->cellChanged(0,0);
        });
        add->setEnabled(table->rowCount()<8);
    };
    for(const auto& filter:initial) append(filter);
    QObject::connect(add,&QPushButton::clicked,panel,[append,table]{append({}); emit table->cellChanged(0,0);});
    return panel;
}
} // namespace

WorkspaceWindow::WorkspaceWindow(Workspace& workspace, const QString& display_path, QWidget* parent)
    : PrototypeWindow(Page{"no-page",{{"placeholder","",BlockKind::Paragraph,{}}}},
          display_path.isEmpty()?q(workspace.directory().u8string()):display_path,false,parent),
      workspace_(workspace),session_(workspace),link_targets_(workspace)
{
    setWindowTitle("Nebula"); resize(1300,850);
    findChild<QLabel*>("prototypeNotice")->hide();
    findChild<QMenu*>("sampleMenu")->menuAction()->setVisible(false);
    for (const auto* name:{"link","image","attachment"}) findChild<QAction*>(name)->setVisible(false);
    auto* page_panel=takeCentralWidget();
    auto* page_layout=qobject_cast<QVBoxLayout*>(page_panel->layout());
    title_=new QLineEdit(page_panel); title_->setObjectName("pageTitle"); title_->setPlaceholderText("Page title");
    title_->setAccessibleName("Page title"); title_->setStyleSheet("font-size: 20px; padding: 6px;");
    save_state_=new QLabel("Select an entry or create a page.",page_panel); save_state_->setObjectName("saveState");
    save_state_->setTextFormat(Qt::PlainText); save_state_->setWordWrap(true);
    auto* page_header=new QWidget(page_panel); auto* page_header_layout=new QHBoxLayout(page_header); page_header_layout->setContentsMargins(0,0,0,0);
    auto* page_trash=new QToolButton(page_header); page_trash->setObjectName("trashPageButton");
    page_header_layout->addWidget(title_,1); page_header_layout->addWidget(page_trash);
    page_layout->insertWidget(1,page_header); page_layout->insertWidget(2,save_state_);
    backlinks_=new QListWidget(page_panel); backlinks_->setObjectName("backlinks"); backlinks_->setMaximumHeight(90);
    backlinks_->setAccessibleName("Referenced by:"); backlinks_->setAccessibleDescription("Pages referencing this page or its blocks, with source context.");
    auto* incoming_heading=new QLabel("Referenced by:",page_panel); incoming_heading->setObjectName("referencedByHeading");
    page_layout->addWidget(incoming_heading); page_layout->addWidget(backlinks_);
    outgoing_=new QListWidget(page_panel); outgoing_->setObjectName("outgoingLinks"); outgoing_->setMaximumHeight(90);
    outgoing_->setAccessibleName("References"); outgoing_->setAccessibleDescription("References from this page.");
    auto* outgoing_heading=new QLabel("References",page_panel); outgoing_heading->setObjectName("referencesHeading");
    page_layout->addWidget(outgoing_heading); page_layout->addWidget(outgoing_);
    auto* navigation=new QWidget;
    auto* navigation_layout=new QVBoxLayout(navigation);
    databases_=new QComboBox(navigation); databases_->setObjectName("databases"); databases_->setAccessibleName("Database");
    views_=new QComboBox(navigation); views_->setObjectName("savedViews"); views_->setAccessibleName("Saved view");
    auto* database_row=new QHBoxLayout;
    auto* database_trash=new QToolButton(navigation); database_trash->setObjectName("trashDatabaseButton");
    database_row->addWidget(databases_,1); database_row->addWidget(database_trash);
    navigation_layout->addLayout(database_row); navigation_layout->addWidget(views_);
    table_=new QTableWidget(navigation); table_->setObjectName("databaseTable"); table_->setAccessibleName("Database entries");
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(true); navigation_layout->addWidget(table_);
    auto* split=new QSplitter(this); split->addWidget(navigation); split->addWidget(page_panel); split->setSizes({450,850}); setCentralWidget(split);
    auto* file=menuBar()->addMenu("&Workspace");
    auto* save=file->addAction("Save now"); save->setObjectName("saveNow"); save->setShortcut(QKeySequence::Save);
    connect(save,&QAction::triggered,this,[this]{ flush(); });
    auto operation=[&](const char* text,const char* name,auto callback) {
        auto* action=file->addAction(text); action->setObjectName(name);
        connect(action,&QAction::triggered,this,[this,callback]{
            try { callback(); } catch(const std::exception& e) { error(this,e); }
        });
        return action;
    };
    operation("Move page to database…","movePage",[this]{ move_page(); });
    operation("Move block to page…","moveBlockToPage",[this]{ move_block(); })->setShortcut(QKeySequence("Ctrl+Shift+M"));
    operation("Undo block transfer","undoBlockTransfer",[this]{ undo_block_transfer(false); })->setShortcut(QKeySequence("Ctrl+Alt+Z"));
    operation("Redo block transfer","redoBlockTransfer",[this]{ undo_block_transfer(true); })->setShortcut(QKeySequence("Ctrl+Alt+Shift+Z"));
    file->addSeparator();
    operation("Keep my version as a new page","saveConflictCopy",[this]{ save_conflict_copy(); })->setEnabled(false);
    operation("Check workspace integrity…","checkIntegrity",[this]{ check_integrity(); });
    auto* navigation_menu=menuBar()->addMenu("&Navigate");
    auto* back=navigation_menu->addAction("Back"); back->setObjectName("navigateBack"); back->setShortcut(QKeySequence("Alt+Left"));
    auto* forward=navigation_menu->addAction("Forward"); forward->setObjectName("navigateForward"); forward->setShortcut(QKeySequence("Alt+Right"));
    auto* follow=navigation_menu->addAction("Follow reference at cursor"); follow->setObjectName("followReference"); follow->setShortcut(QKeySequence("Ctrl+Return"));
    connect(back,&QAction::triggered,this,[this]{ navigate_history(false); });
    connect(forward,&QAction::triggered,this,[this]{ navigate_history(true); });
    connect(follow,&QAction::triggered,this,[this]{ if (page_editor()->hasFocus()) page_editor()->activate_current_reference(); });
    update_navigation_actions();
    auto* attachments_menu=menuBar()->addMenu("&Attachments");
    auto attachment_action=[&](const char* text,const char* name,auto callback) {
        auto* action=attachments_menu->addAction(text); action->setObjectName(name);
        connect(action,&QAction::triggered,this,[this,callback]{ try { callback(); } catch(const std::exception& e) { error(this,e); } });
    };
    attachment_action("Import image…","importImage",[this]{ import_attachment(true); });
    attachment_action("Import file…","importFile",[this]{ import_attachment(false); });
    attachment_action("Open current attachment","openAttachment",[this]{ open_attachment(); });
    attachment_action("Save a copy…","exportAttachment",[this]{ export_attachment(); });
    attachment_action("Check managed files…","auditAttachments",[this]{ show_attachment_audit(); });
    auto* search_menu=menuBar()->addMenu("&Search");
    auto* interchange_menu=menuBar()->addMenu("&Import / Export");
    for (const bool importing:{true,false}) {
        auto* action=interchange_menu->addAction(importing?"Import Markdown folder…":"Export Markdown folder…");
        action->setObjectName(importing?"importMarkdown":"exportMarkdown");
        connect(action,&QAction::triggered,this,[this,importing] {try {markdown_interchange(importing);} catch(const std::exception& e){error(this,e);}});
        auto* json=interchange_menu->addAction(importing?"Import lossless JSON into new workspace…":"Export lossless JSON folder…");
        json->setObjectName(importing?"importJson":"exportJson");
        connect(json,&QAction::triggered,this,[this,importing] {try {json_interchange(importing);} catch(const std::exception& e){error(this,e);}});
    }
    auto* search=search_menu->addAction("Find in workspace…"); search->setObjectName("searchWorkspace"); search->setShortcut(QKeySequence("Ctrl+Shift+F"));
    connect(search,&QAction::triggered,this,[this]{ try { show_search(); } catch(const std::exception& e){error(this,e);} });
    auto* rebuild=search_menu->addAction("Rebuild search and reference indexes"); rebuild->setObjectName("rebuildSearch");
    connect(rebuild,&QAction::triggered,this,[this]{
        try { workspace_.rebuild_links(); workspace_.rebuild_search(); link_targets_.invalidate(); if (flush()) statusBar()->showMessage("Search and reference indexes rebuilt from saved notes.",6000); }
        catch(const std::exception& e) {error(this,e);}
    });
    addToolBarBreak();
    auto* bar=addToolBar("Database controls"); bar->setObjectName("databaseControls"); bar->setMovable(false);
    auto add=[&](const char* text,const char* name,auto callback) {
        auto* action=bar->addAction(text); action->setObjectName(name);
        connect(action,&QAction::triggered,this,[this,callback]{ try { callback(); } catch(const std::exception& e) { error(this,e); } });
    };
    add("New database","newDatabase",[this]{
        if (!flush()) return;
        bool ok=false; const auto name=QInputDialog::getText(this,"New database","Name",QLineEdit::Normal,"Notes",&ok);
        if (ok && !name.trimmed().isEmpty()) { auto db=workspace_.create_database(s(name.trimmed())); link_targets_.invalidate(); refresh_databases(); databases_->setCurrentIndex(databases_->findData(q(db.id))); }
    });
    add("Rename database","renameDatabase",[this]{
        bool ok=false; const auto name=QInputDialog::getText(this,"Rename database","Name",QLineEdit::Normal,databases_->currentText(),&ok);
        if (ok && !name.trimmed().isEmpty()) { workspace_.rename_database(database_id_,s(name.trimmed())); link_targets_.invalidate(); refresh_databases(); }
    });
    add("New page","newPage",[this]{
        bool ok=false; const auto title=QInputDialog::getText(this,"New page","Title",QLineEdit::Normal,"Untitled",&ok);
        if (ok) create_page(title);
    });
    add("Journal entry","newJournalEntry",[this]{
        if (!flush() || database_id_.empty()) return;
        bool ok=false; const auto date=QInputDialog::getText(this,"Journal entry","Entry date (YYYY-MM-DD)",QLineEdit::Normal,QDate::currentDate().toString(Qt::ISODate),&ok);
        if (!ok) return;
        // The Entry date property (if missing) and the page commit together.
        auto page=workspace_.create_dated_entry(database_id_,s(date.trimmed()));
        link_targets_.invalidate(); refresh_table(); open_page(page.document.id);
    });
    add("Add property","addProperty",[this]{ add_property(); });
    add("Rename property","renameProperty",[this]{ rename_property(); });
    add("Delete property","deleteProperty",[this]{ delete_property(); });
    add("New view","newView",[this]{ edit_view(true); });
    add("Edit view","editView",[this]{ edit_view(false); });
    add("Delete view","deleteView",[this]{ delete_view(); });
    add("Link…","insertLink",[this]{ insert_link(); });
    auto* block_trash_action=operation("Move block to Trash…","trashBlock",[this]{ trash_current_block(); });
    auto* page_trash_action=operation("Move page to Trash…","trashPage",[this]{ trash_current_page(); });
    auto* database_trash_action=operation("Move database to Trash…","trashDatabase",[this]{ trash_current_database(); });
    configure_trash_button(page_trash,page_trash_action,TrashScope::Page);
    configure_trash_button(database_trash,database_trash_action,TrashScope::Database);
    auto* block_bar=findChild<QToolBar*>("blockControls"); block_bar->addSeparator();
    auto* block_trash=new QToolButton(block_bar); block_trash->setObjectName("trashBlockButton");
    configure_trash_button(block_trash,block_trash_action,TrashScope::Block); block_bar->addWidget(block_trash);
    add("Restore…","restoreTrash",[this]{ show_trash(); });
    autosave_=new QTimer(this); autosave_->setSingleShot(true); autosave_->setInterval(500);
    // Table refreshes triggered by saves run outside item signals and wait for open cell editors.
    table_refresh_=new QTimer(this); table_refresh_->setSingleShot(true); table_refresh_->setInterval(0);
    connect(table_refresh_,&QTimer::timeout,this,[this]{
        if (table_editing(table_)) { table_refresh_->start(300); return; }
        refresh_table();
    });
    connect(autosave_,&QTimer::timeout,this,[this]{ flush(); });
    connect(page_editor(),&QTextEdit::textChanged,this,&WorkspaceWindow::mark_pending);
    new PastedLinkPreview(page_editor(),statusBar(),this);
    connect(page_editor(),&QTextEdit::cursorPositionChanged,this,&WorkspaceWindow::update_attachment_actions);
    connect(page_editor(),&QTextEdit::cursorPositionChanged,this,&WorkspaceWindow::update_trash_actions);
    connect(title_,&QLineEdit::textEdited,this,[this](const QString& title){
        if (!loading_) { page_editor()->set_page_title(title); mark_pending(); }
    });
    connect(databases_,&QComboBox::currentIndexChanged,this,[this]{ if (!loading_) switch_database(); });
    connect(views_,&QComboBox::currentIndexChanged,this,[this](int i){ if (!loading_ && i>=0 && i<int(saved_views_.size())) { current_view_=saved_views_[std::size_t(i)]; refresh_table(); } });
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{
        if (!loading_ && table_->currentRow()>=0) open_page(s(table_->item(table_->currentRow(),0)->data(Qt::UserRole).toString()));
    });
    connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if (loading_) return;
        in_item_change_=true; struct Reset { bool& flag; ~Reset() { flag=false; } } reset{in_item_change_};
        try {
            const auto id=s(table_->item(item->row(),0)->data(Qt::UserRole).toString());
            const int column=item->column();
            const auto text=item->text();
            const auto property_id=column?s(table_->horizontalHeaderItem(column)->data(Qt::UserRole).toString()):std::string{};
            if (!open_page(id)) return;
            if (column==0) { session_.set_title(s(text)); QSignalBlocker guard(title_); title_->setText(text); page_editor()->set_page_title(text); }
            else {
                const auto def=std::find_if(definitions_.begin(),definitions_.end(),[&](const auto& d){return d.id==property_id;});
                if (def!=definitions_.end()) session_.set_property(property_id,parse_value(def->kind,text));
            }
            flush();
        } catch (const std::exception& e) { error(this,e); schedule_table_refresh(); }
    });
    disconnect(page_editor(),&PageEditor::reference_activated,this,nullptr);
    connect(page_editor(),&PageEditor::reference_activated,this,[this](int kind,const QString& id){ follow_reference(ReferenceKind(kind),s(id)); });
    link_completer_=new QCompleter(this); link_completer_->setObjectName("wikiCompleter");
    completion_model_=new QStandardItemModel(this); link_completer_->setModel(completion_model_);
    link_completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    link_completer_->setMaxVisibleItems(12); link_completer_->popup()->setObjectName("wikiCompletion");
    link_completer_->popup()->setAccessibleName("Wiki link targets"); page_editor()->set_wiki_completer(link_completer_);
    connect(page_editor(),&PageEditor::wiki_query_changed,this,&WorkspaceWindow::complete_wiki);
    connect(page_editor(),&PageEditor::wiki_link_requested,this,&WorkspaceWindow::enter_wiki_link);
    connect(link_completer_,QOverload<const QModelIndex&>::of(&QCompleter::activated),this,[this](const QModelIndex& index){
        const auto prefix=page_editor()->wiki_prefix(); if (!prefix) return;
        const auto syntax=parse_wiki_link(s("[["+*prefix+"]]"));
        if (!page_editor()->select_wiki_link()) return;
        page_editor()->insert_reference(syntax && syntax->custom_label?q(syntax->label):index.data(Qt::UserRole+2).toString(),
            ReferenceKind(index.data(Qt::UserRole).toInt()),index.data(Qt::UserRole+1).toString(),syntax && syntax->custom_label);
        completing_=false;
    });
    page_editor()->set_reference_label_resolver([this](const Reference& reference){ return workspace_.reference_label(reference); });
    page_editor()->set_attachment_resolver([this](const std::string& id) -> std::optional<std::string> {
        try { return workspace_.attachment_path(id).u8string(); } catch (const std::exception&) { return {}; }
    });
    connect(page_editor(),&PageEditor::unresolved_reference_activated,this,[this](const QString& name,const QString& scope){
        try { resolve_unresolved(name,scope); } catch(const std::exception& e) { error(this,e); }
    });
    connect(backlinks_,&QListWidget::itemActivated,this,[this](QListWidgetItem* item){ open_page(s(item->data(Qt::UserRole).toString()),s(item->data(Qt::UserRole+1).toString())); });
    connect(outgoing_,&QListWidget::itemActivated,this,[this](QListWidgetItem* item){
        const auto id=item->data(Qt::UserRole+1).toString();
        if (id.isEmpty()) {
            page_editor()->select_reference_at({s(item->data(Qt::UserRole+2).toString()),std::size_t(item->data(Qt::UserRole+5).toULongLong()),1}); page_editor()->setFocus();
            page_editor()->unresolved_reference_activated(item->data(Qt::UserRole+3).toString(),item->data(Qt::UserRole+4).toString());
        } else follow_reference(ReferenceKind(item->data(Qt::UserRole).toInt()),s(id));
    });
    // The default Journal, its properties and view are created in one transaction.
    if (workspace_.databases(true).empty()) workspace_.create_journal();
    refresh_databases();
}
void WorkspaceWindow::mark_pending()
{
    if (loading_ || !session_.has_page()) return;
    if (title_->text()!=page_editor()->page_title()) { QSignalBlocker guard(title_); title_->setText(page_editor()->page_title()); }
    pending_=true; save_state_->setText("Unsaved changes…"); autosave_->start();
}
bool WorkspaceWindow::flush()
{
    if (loading_ || !session_.has_page()) return true;
    autosave_->stop();
    if (!pending_ && !session_.dirty()) return true;
    try {
        if (pending_) {
            session_.replace_document(page_editor()->snapshot()); session_.set_title(s(title_->text()));
            session_.note_absorbed(page_editor()->take_absorbed_blocks()); pending_=false;
        }
        if (!session_.save()) throw StorageError(session_.error_kind(),session_.error());
        findChild<QAction*>("saveConflictCopy")->setEnabled(false);
        save_state_->setText("Saved locally"); refresh_after_save(); return true;
    } catch (const std::exception& e) {
        const bool conflict=error_kind(e)==ErrorKind::Conflict;
        findChild<QAction*>("saveConflictCopy")->setEnabled(conflict);
        save_state_->setText(conflict
            ?"Save conflict — this page changed elsewhere. Use Workspace → Keep my version as a new page to keep these edits."
            :"Save failed — "+q(e.what())+". Press Ctrl+S to retry.");
        return false;
    }
}
namespace {
std::string row_signature(const PageRecord& page) {
    std::string result=page.document.id+"\x1f"+page.title;
    for (const auto& entry:page.properties) result+="\x1f"+entry.first+"="+format_property(entry.second);
    return result;
}
std::string link_signature(const PageRecord& page) {
    std::string result=page.title;
    for (const auto& block:page.document.blocks) for (const auto& run:block.runs) if (run.reference)
        result+="\x1f"+std::to_string(int(run.reference->kind))+run.reference->target_id+"|"+run.reference->unresolved_name+"|"+run.text;
    return result;
}
} // namespace
void WorkspaceWindow::schedule_table_refresh() { table_refresh_->start(0); }
// Typing in the page body changes neither its table row nor its link lists; refresh
// only what the saved record could have changed.
void WorkspaceWindow::refresh_after_save()
{
    if (!session_.has_page()) return;
    if (row_signature(session_.record())!=rendered_row_) {
        if (in_item_change_ || table_editing(table_)) schedule_table_refresh(); else refresh_table();
    }
    const auto links=link_signature(session_.record());
    if (links!=link_signature_) update_links();
}
void WorkspaceWindow::refresh_databases()
{
    const auto previous=database_id_; loading_=true; databases_->clear(); link_targets_.invalidate();
    for (const auto& db:workspace_.databases()) databases_->addItem(q(db.name),q(db.id));
    const int row=databases_->findData(q(previous)); if (row>=0) databases_->setCurrentIndex(row);
    loading_=false; switch_database();
}
void WorkspaceWindow::switch_database()
{
    if (!flush()) { QSignalBlocker guard(databases_); databases_->setCurrentIndex(databases_->findData(q(database_id_))); return; }
    if (session_.has_page() && !history_navigation_) detached_position_=current_position();
    session_.close(); pending_=false; loading_=true;
    database_id_=s(databases_->currentData().toString());
    page_editor()->set_page({"no-page",{{"placeholder","",BlockKind::Paragraph,{}}}});
    page_editor()->setEnabled(false); title_->clear(); title_->setEnabled(false); backlinks_->clear(); outgoing_->clear();
    update_transfer_actions();
    for (auto* bar:findChildren<QToolBar*>()) if (bar->objectName()!="databaseControls") bar->setEnabled(false);
    save_state_->setText("Select an entry or create a page.");
    definitions_=workspace_.properties(database_id_); saved_views_=workspace_.views(database_id_);
    if (saved_views_.empty()) {
        current_view_={}; current_view_.database_id=database_id_; current_view_.name="All entries";
        for (const auto& d:definitions_) current_view_.visible_properties.push_back(d.id);
        if (!database_id_.empty()) { workspace_.save_view(current_view_); saved_views_.push_back(current_view_); }
    }
    else current_view_=saved_views_.front();
    views_->clear(); for (const auto& view:saved_views_) views_->addItem(q(view.name));
    if (saved_views_.empty()) views_->addItem("All entries");
    loading_=false; refresh_table();
}
void WorkspaceWindow::refresh_table()
{
    const bool old_loading=loading_; loading_=true;
    const auto selected=session_.has_page()?session_.record().document.id:std::string{};
    definitions_=workspace_.properties(database_id_);
    const auto rows=workspace_.query_view(current_view_);
    std::vector<PropertyDefinition> visible;
    for (const auto& id:current_view_.visible_properties)
        for (const auto& def:definitions_) if (def.id==id) visible.push_back(def);
    table_->clear(); table_->setRowCount(int(rows.size())); table_->setColumnCount(int(visible.size()+1));
    table_->setHorizontalHeaderItem(0,new QTableWidgetItem("Title"));
    for (std::size_t c=0;c<visible.size();++c) { auto* item=new QTableWidgetItem(q(visible[c].name)); item->setData(Qt::UserRole,q(visible[c].id)); table_->setHorizontalHeaderItem(int(c+1),item); }
    for (std::size_t r=0;r<rows.size();++r) {
        auto* title=new QTableWidgetItem(q(rows[r].title)); title->setData(Qt::UserRole,q(rows[r].document.id)); table_->setItem(int(r),0,title);
        for (std::size_t c=0;c<visible.size();++c) {
            const auto found=rows[r].properties.find(visible[c].id);
            table_->setItem(int(r),int(c+1),new QTableWidgetItem(found==rows[r].properties.end()?QString{}:display(found->second)));
        }
        if (rows[r].document.id==selected) table_->selectRow(int(r));
    }
    rendered_row_=session_.has_page()?row_signature(session_.record()):std::string{};
    loading_=old_loading;
}
bool WorkspaceWindow::open_page(const std::string& id, const std::string& block)
{
    try {
        const auto previous=current_position();
        if (session_.has_page() && session_.record().document.id==id) {
            if (!block.empty()) {
                if (!flush() || !page_editor()->go_to_block(q(block))) return false;
                remember_navigation(previous); page_editor()->setFocus();
            }
            return true;
        }
        if (!flush()) return false;
        const auto target=workspace_.load_page(id);
        if (target.trashed) throw std::runtime_error("This page is in trash; restore it first");
        const int database_index=databases_->findData(q(target.database_id));
        if (database_index<0) throw std::runtime_error("This page's database is in trash");
        if (target.database_id!=database_id_) { databases_->setCurrentIndex(database_index); }
        session_.open(id); loading_=true;
        completing_=false;
        title_->setText(q(session_.record().title)); title_->setEnabled(true); page_editor()->set_page(session_.record().document,q(session_.record().title)); page_editor()->setEnabled(true);
        for (auto* bar:findChildren<QToolBar*>()) bar->setEnabled(true);
        loading_=false; pending_=false; save_state_->setText("Saved locally");
        findChild<QAction*>("saveConflictCopy")->setEnabled(false);
        if (in_item_change_) schedule_table_refresh(); else refresh_table();
        update_links(); update_transfer_actions();
        if (!block.empty()) page_editor()->go_to_block(q(block));
        remember_navigation(previous); detached_position_.reset();
        page_editor()->setFocus(); return true;
    } catch (const std::exception& e) { save_state_->setText(q(e.what())); return false; }
}
void WorkspaceWindow::create_page(const QString& title)
{
    if (!flush() || database_id_.empty()) return;
    const auto page=workspace_.create_page(database_id_,s(title)); link_targets_.invalidate(); refresh_table(); open_page(page.document.id);
}
void WorkspaceWindow::update_links()
{
    backlinks_->clear(); outgoing_->clear(); link_signature_.clear(); if (!session_.has_page()) return;
    link_signature_=link_signature(session_.record());
    for (const auto& edge:workspace_.backlinks(ReferenceKind::Page,session_.record().document.id)) {
        auto* item=new QListWidgetItem(q(edge.source_title)+" — "+q(edge.source_context).left(100)
            +" → "+(edge.kind==ReferenceKind::Page?QString("page"):"block ["+q(edge.target_id)+"]"),backlinks_);
        item->setData(Qt::UserRole,q(edge.source_page)); item->setData(Qt::UserRole+1,q(edge.source_block));
        item->setToolTip("Source block: "+q(edge.source_block)+"\nTarget: "+q(edge.target_id));
    }
    for (const auto& edge:workspace_.outgoing_links(session_.record().document.id)) {
        QString destination;
        if (edge.target_id.empty()) destination="Unresolved: "+q(edge.unresolved_name);
        else {
            const auto target=workspace_.resolve_reference({edge.kind,edge.target_id});
            if (!target.page_id.empty()) destination=q(workspace_.page_title(target.page_id).value_or(std::string{}));
            if (edge.kind==ReferenceKind::Block) destination+=" / block ["+q(edge.target_id)+"]";
            if (target.state==ReferenceState::PageTrashed || target.state==ReferenceState::DatabaseTrashed) destination+=" (in trash)";
            else if (target.state!=ReferenceState::Available) destination="Unavailable ["+q(edge.target_id)+"]";
        }
        auto* item=new QListWidgetItem(q(edge.label)+" → "+destination+" — "+q(edge.source_context).left(100),outgoing_);
        item->setData(Qt::UserRole,int(edge.kind)); item->setData(Qt::UserRole+1,q(edge.target_id));
        item->setData(Qt::UserRole+2,q(edge.source_block)); item->setData(Qt::UserRole+3,q(edge.unresolved_name)); item->setData(Qt::UserRole+4,q(edge.database_scope));
        item->setData(Qt::UserRole+5,qlonglong(edge.run_index));
        item->setToolTip("Source block: "+q(edge.source_block)+"\nTarget: "+q(edge.target_id));
    }
}
std::optional<WorkspaceWindow::NavigationPosition> WorkspaceWindow::current_position() const
{
    if (session_.has_page()) return NavigationPosition{session_.record().document.id,page_editor()->capture_position()};
    return detached_position_;
}
void WorkspaceWindow::update_navigation_actions()
{
    findChild<QAction*>("navigateBack")->setEnabled(history_.can_go(false));
    findChild<QAction*>("navigateForward")->setEnabled(history_.can_go(true));
}
void WorkspaceWindow::remember_navigation(const std::optional<NavigationPosition>& previous)
{
    if (history_navigation_ || !previous || !session_.has_page()) return;
    if (history_.record(*previous,{session_.record().document.id,page_editor()->capture_position()})) update_navigation_actions();
}
void WorkspaceWindow::navigate_history(bool forward)
{
    const auto target=history_.peek(forward); if (!target) return;
    const auto previous=current_position();
    history_navigation_=true;
    const bool opened=open_page(target->page);
    history_navigation_=false;
    if (!opened) { statusBar()->showMessage("Cannot open history destination: "+save_state_->text(),8000); return; }
    const bool restored=page_editor()->restore_position(target->editor);
    history_.commit(forward,previous);
    update_navigation_actions(); page_editor()->setFocus();
    if (!restored) statusBar()->showMessage("The previous block is unavailable; opened its original page.",8000);
}
void WorkspaceWindow::add_property()
{
    if (!flush() || database_id_.empty()) return;
    bool ok=false; const auto name=QInputDialog::getText(this,"Add property","Name",QLineEdit::Normal,{},&ok); if (!ok || name.trimmed().isEmpty()) return;
    const QStringList kinds{"Text","Number","Date","Checkbox","Tags"};
    const auto selected=QInputDialog::getItem(this,"Property type","Type",kinds,0,false,&ok); if (!ok) return;
    const auto def=workspace_.add_property(database_id_,s(name.trimmed()),PropertyKind(kinds.indexOf(selected)));
    current_view_.visible_properties.push_back(def.id); workspace_.save_view(current_view_);
    saved_views_=workspace_.views(database_id_); refresh_table();
}
void WorkspaceWindow::edit_view(bool create)
{
    if (!flush() || database_id_.empty()) return;
    QDialog dialog(this); dialog.setWindowTitle(create?"New saved view":"Edit saved view");
    auto* layout=new QVBoxLayout(&dialog); auto* form=new QFormLayout; layout->addLayout(form);
    auto* name=new QLineEdit(create?"New view":q(current_view_.name),&dialog); name->setObjectName("viewName"); form->addRow("Name",name);
    auto* sort=new QComboBox(&dialog); sort->setObjectName("viewSort"); sort->addItem("Title",QString{}); for (const auto& d:definitions_) sort->addItem(q(d.name),q(d.id)); sort->setCurrentIndex(sort->findData(q(current_view_.sort_property))); form->addRow("Sort",sort);
    auto* ascending=new QCheckBox("Ascending",&dialog); ascending->setChecked(current_view_.ascending); form->addRow(ascending);
    auto* visible=new QListWidget(&dialog); for (const auto& d:definitions_) { auto* item=new QListWidgetItem(q(d.name),visible); item->setData(Qt::UserRole,q(d.id)); item->setCheckState(std::find(current_view_.visible_properties.begin(),current_view_.visible_properties.end(),d.id)!=current_view_.visible_properties.end()?Qt::Checked:Qt::Unchecked); } form->addRow("Visible properties",visible);
    auto* filters=new QTableWidget(0,3,&dialog); filters->setObjectName("viewFilters"); filters->setHorizontalHeaderLabels({"Property","Condition","Value"}); layout->addWidget(filters);
    auto add_filter=[&](const Filter& filter) {
        const int row=filters->rowCount(); filters->insertRow(row);
        auto* property=new QComboBox(filters); property->addItem("Title",QString{}); for (const auto& d:definitions_) property->addItem(q(d.name),q(d.id)); property->setCurrentIndex(property->findData(q(filter.property_id)));
        auto* op=new QComboBox(filters); op->addItems({"Equals","Contains","Less than","Greater than","Is empty","Is not empty"}); op->setCurrentIndex(int(filter.comparison));
        filters->setCellWidget(row,0,property); filters->setCellWidget(row,1,op); filters->setCellWidget(row,2,new QLineEdit(display(filter.value),filters));
    };
    for (const auto& filter:current_view_.filters) add_filter(filter);
    auto* more=new QPushButton("Add filter",&dialog); more->setObjectName("addViewFilter"); layout->addWidget(more); connect(more,&QPushButton::clicked,&dialog,[&]{ add_filter({}); });
    auto* remove=new QPushButton("Remove selected filter",&dialog); layout->addWidget(remove); connect(remove,&QPushButton::clicked,&dialog,[&]{ if (filters->currentRow()>=0) filters->removeRow(filters->currentRow()); });
    auto* references=link_filter_controls(&dialog,workspace_,current_view_.link_filters); layout->addWidget(references);
    auto* message=new QLabel(&dialog); message->setTextFormat(Qt::PlainText); layout->addWidget(message);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        try {
            ViewEdit edit; auto& view=edit.settings; if (!create) view.id=current_view_.id; view.database_id=database_id_; view.name=s(name->text().trimmed()); view.sort_property=s(sort->currentData().toString()); view.ascending=ascending->isChecked();
            view.link_filters=selected_link_filters(references->findChild<QTableWidget*>("linkFilters"));
            for (int i=0;i<visible->count();++i) if (visible->item(i)->checkState()==Qt::Checked) view.visible_properties.push_back(s(visible->item(i)->data(Qt::UserRole).toString()));
            for (int r=0;r<filters->rowCount();++r) {
                const auto id=s(qobject_cast<QComboBox*>(filters->cellWidget(r,0))->currentData().toString());
                const auto op=Comparison(qobject_cast<QComboBox*>(filters->cellWidget(r,1))->currentIndex());
                const auto text=qobject_cast<QLineEdit*>(filters->cellWidget(r,2))->text();
                edit.filters.push_back({id,op,s(text)});
            }
            view=save_view_edit(workspace_,edit); current_view_=view; saved_views_=workspace_.views(database_id_);
            loading_=true; views_->clear(); int index=0;
            for (std::size_t i=0;i<saved_views_.size();++i) { views_->addItem(q(saved_views_[i].name)); if (saved_views_[i].id==view.id) index=int(i); }
            views_->setCurrentIndex(index); loading_=false; refresh_table(); dialog.accept();
        } catch (const std::exception& e) { message->setText(q(e.what())); }
    });
    dialog.resize(850,700); dialog.exec();
}
void WorkspaceWindow::show_trash()
{
    if (!flush()) return;
    const auto reopen=session_.has_page()?session_.record().document.id:std::string{};
    QDialog dialog(this); dialog.setWindowTitle("Restore from trash"); auto* layout=new QVBoxLayout(&dialog); auto* list=new QListWidget(&dialog); list->setObjectName("trashItems"); layout->addWidget(list);
    for (const auto& db:workspace_.databases(true))
        if (db.trashed) { auto* item=new QListWidgetItem("Database: "+q(db.name),list); item->setData(Qt::UserRole,q(db.id)); item->setData(Qt::UserRole+1,true); }
    for (const auto& page:workspace_.page_summaries(true))
        if (page.trashed) { auto* item=new QListWidgetItem(q(page.database_name)+" / "+q(page.title),list); item->setData(Qt::UserRole,q(page.id)); }
    for (const auto& group:workspace_.deleted_blocks()) {
        auto* item=new QListWidgetItem("Blocks in "+q(group.page_title)+": "+q(group.label).left(60),list);
        item->setToolTip("Restoring brings these blocks back to their page");
        item->setData(Qt::UserRole,q(group.id)); item->setData(Qt::UserRole+2,"blocks");
    }
    auto* restore=new QPushButton("Restore selected",&dialog); restore->setObjectName("restoreSelected"); layout->addWidget(restore);
    connect(restore,&QPushButton::clicked,&dialog,[&]{
        if (auto* item=list->currentItem()) {
            try {
                const auto id=s(item->data(Qt::UserRole).toString());
                bool reparented=false;
                if (item->data(Qt::UserRole+2).toString()=="blocks") {
                    workspace_.restore_deleted_blocks(id,&reparented);
                    if (reparented) notify(&dialog,QMessageBox::Information,"Blocks restored","The original parent block is no longer on this page, so the blocks were restored as top-level blocks.");
                }
                else if (item->data(Qt::UserRole+1).toBool()) workspace_.trash_database(id,false);
                else workspace_.trash_page(id,false);
                link_targets_.invalidate(); delete item;
            } catch (const std::exception& e) { error(&dialog,e); }
        }
    });
    auto* purge=new QPushButton("Empty trash permanently…",&dialog); purge->setObjectName("emptyTrash"); layout->addWidget(purge);
    connect(purge,&QPushButton::clicked,&dialog,[&]{
        try { empty_trash(&dialog); dialog.accept(); } catch (const std::exception& e) { error(&dialog,e); }
    });
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(close); connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::accept); dialog.exec(); refresh_databases();
    // Return to the page that was open, reloaded with any restored blocks.
    if (!reopen.empty() && workspace_.resolve_reference({ReferenceKind::Page,reopen}).state==ReferenceState::Available) open_page(reopen);
}
std::vector<WorkspaceWindow::LinkCandidate> WorkspaceWindow::link_candidates() const
{
    // Cached lightweight rows; the open page contributes its unsaved blocks.
    std::vector<LinkCandidate> result; std::vector<LinkTargetRow> rows;
    if (session_.has_page()) {
        auto current=session_.record(); current.document=page_editor()->snapshot();
        rows=link_targets_.rows(&current,s(title_->text()),s(databases_->currentText()));
    } else rows=link_targets_.rows();
    result.reserve(rows.size());
    for (const auto& row:rows) result.push_back({row.kind,row.id,row.label,row.page_id,row.page_title,row.database_name,row.heading});
    return result;
}
void WorkspaceWindow::complete_wiki(const QString& prefix, bool active)
{
    if (!active || !session_.has_page()) {
        link_completer_->popup()->hide(); completing_=false; return;
    }
    if (!completing_) { completion_targets_=link_candidates(); completing_=true; }
    completion_model_->clear();
    const auto parsed=parse_wiki_link(s("[["+prefix+"]]"));
    auto query=parsed?q(parsed->page)+" "+q(parsed->heading)+" "+q(parsed->block):prefix.section('|',0,0);
    if (!parsed) { query.replace('#',' '); query.remove('^'); }
    const auto words=query.split(' ',Qt::SkipEmptyParts);
    for (const auto& candidate:completion_targets_) {
        const auto text=q(candidate.database)+" / "+q(candidate.title)
            +(candidate.kind==ReferenceKind::Page?QString{}:(candidate.heading?" / Heading: ":" / Block: ")+q(candidate.label).left(100))
            +" ["+q(candidate.id)+"]";
        if (!std::all_of(words.begin(),words.end(),[&](const auto& word){return text.contains(word,Qt::CaseInsensitive);})) continue;
        auto* item=new QStandardItem(text); item->setData(int(candidate.kind),Qt::UserRole);
        item->setData(q(candidate.id),Qt::UserRole+1); item->setData(q(candidate.label),Qt::UserRole+2);
        completion_model_->appendRow(item);
        if (completion_model_->rowCount()>=100) break;
    }
    if (!completion_model_->rowCount()) { link_completer_->popup()->hide(); return; }
    link_completer_->setCompletionPrefix({});
    link_completer_->popup()->setCurrentIndex(link_completer_->completionModel()->index(0,0));
    auto rect=page_editor()->cursorRect(); rect.setWidth(std::min(800,std::max(400,link_completer_->popup()->sizeHintForColumn(0)+30)));
    link_completer_->complete(rect);
}
void WorkspaceWindow::enter_wiki_link(const QString& syntax)
{
    completing_=false; const auto link=parse_wiki_link(s(syntax));
    auto leave_text=[this] { auto cursor=page_editor()->textCursor(); cursor.clearSelection(); page_editor()->setTextCursor(cursor); };
    if (!link) { leave_text(); return; }
    std::vector<LinkCandidate> matches;
    for (const auto& candidate:link_candidates()) {
        const bool page=link->page.empty()?candidate.page==session_.record().document.id:candidate.title==link->page;
        if (!page) continue;
        if (!link->heading.empty() && candidate.kind==ReferenceKind::Block && candidate.heading && candidate.label==link->heading) matches.push_back(candidate);
        else if (!link->block.empty() && candidate.kind==ReferenceKind::Block && candidate.id==link->block) matches.push_back(candidate);
        else if (link->heading.empty() && link->block.empty() && candidate.kind==ReferenceKind::Page) matches.push_back(candidate);
    }
    if (matches.size()==1) {
        const auto& target=matches.front(); page_editor()->insert_reference(q(link->custom_label?link->label:target.label),target.kind,q(target.id),link->custom_label);
    } else if (matches.empty() && link->heading.empty() && link->block.empty()) {
        page_editor()->insert_unresolved_reference(q(link->custom_label?link->label:link->page),q(link->page),q(database_id_),link->custom_label);
    } else if (matches.empty()) {
        leave_text(); statusBar()->showMessage("No matching heading or block. Use Link… to choose its target.",8000);
    } else {
        insert_link(q(link->page)+" "+q(link->heading)+" "+q(link->block),link->custom_label?q(link->label):QString{});
        leave_text();
    }
}
void WorkspaceWindow::insert_link(const QString& query, const QString& custom_label)
{
    if (!session_.has_page()) return;
    QDialog dialog(this); dialog.setWindowTitle("Link to a page, heading or block"); auto* layout=new QVBoxLayout(&dialog);
    auto* search=new QLineEdit(&dialog); search->setObjectName("linkSearch"); search->setPlaceholderText("Search database, page, heading, block text or ID"); search->setAccessibleName("Find link target"); layout->addWidget(search);
    std::vector<LinkTargetRow> targets;
    for (const auto& target : link_candidates())
        targets.push_back({target.kind,target.id,target.label,target.page,target.title,target.database,target.heading});
    auto* tree=new LinkTargetTree(std::move(targets),&dialog); layout->addWidget(tree);
    auto* label=new QLineEdit(&dialog); label->setObjectName("linkLabel"); label->setText(custom_label); label->setPlaceholderText("Optional custom label"); layout->addWidget(label);
    auto* unresolved=new QLineEdit(&dialog); unresolved->setObjectName("unresolvedName"); unresolved->setPlaceholderText("Name of an unresolved page (does not create it)"); layout->addWidget(unresolved);
    auto* keep_unresolved=new QPushButton("Insert unresolved link",&dialog); keep_unresolved->setObjectName("insertUnresolved"); layout->addWidget(keep_unresolved);
    connect(keep_unresolved,&QPushButton::clicked,&dialog,[&]{
        if (!unresolved->text().trimmed().isEmpty()) {
            const auto name=unresolved->text().trimmed(); page_editor()->insert_unresolved_reference(label->text().isEmpty()?name:label->text(),name,q(database_id_),!label->text().isEmpty()); dialog.accept();
        }
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(tree,&QTreeWidget::currentItemChanged,&dialog,[&](QTreeWidgetItem* item){ buttons->button(QDialogButtonBox::Ok)->setEnabled(item && !item->isHidden() && !item->data(0,Qt::UserRole).toString().isEmpty()); });
    connect(search,&QLineEdit::textChanged,tree,&LinkTargetTree::search);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{ if (auto* item=tree->currentItem(); item && !item->isHidden() && !item->data(0,Qt::UserRole).toString().isEmpty()) {
        const auto text=label->text().isEmpty()?item->data(0,Qt::UserRole+2).toString():label->text();
        const Reference reference{ReferenceKind(item->data(0,Qt::UserRole+1).toInt()),s(item->data(0,Qt::UserRole).toString()),"","",!label->text().isEmpty()};
        if (page_editor()->textCursor().hasSelection() && page_editor()->selected_reference_location()) page_editor()->replace_selected_reference(reference,text);
        else page_editor()->insert_reference(text,reference.kind,q(reference.target_id),reference.custom_label);
        dialog.accept();
    } });
    connect(tree,&QTreeWidget::itemDoubleClicked,&dialog,[&]{ buttons->button(QDialogButtonBox::Ok)->click(); });
    search->setText(query.trimmed()); dialog.resize(760,550); dialog.exec(); activateWindow(); page_editor()->setFocus();
}
void WorkspaceWindow::resolve_unresolved(const QString& name, const QString& scope)
{
    if (!session_.has_page() || !flush()) return;
    auto* editor=page_editor();
    auto reference=editor->current_reference();
    if (!reference || !reference->target_id.empty() || reference->unresolved_name!=s(name) || reference->database_scope!=s(scope)) {
        const auto page=editor->snapshot(); bool found=false;
        for (const auto& block:page.blocks) {
            for (std::size_t i=0;i<block.runs.size();++i) {
                const auto& ref=block.runs[i].reference;
                if (ref && ref->target_id.empty() && ref->unresolved_name==s(name) && ref->database_scope==s(scope)) {
                    found=editor->select_reference_at({block.id,i,1}); break;
                }
            }
            if (found) break;
        }
        if (!found) return;
        reference=editor->current_reference();
    }
    if (!editor->select_current_reference()) return;
    const auto location=editor->selected_reference_location(); if (!location) return;
    const auto old_label=editor->textCursor().selectedText();
    QDialog dialog(this); dialog.setObjectName("resolveUnresolved"); dialog.setWindowTitle("Resolve unresolved reference");
    auto* layout=new QVBoxLayout(&dialog); auto* intended=new QLabel("Intended page: "+name+"\nChoose an existing target or explicitly create a page.",&dialog);
    intended->setTextFormat(Qt::PlainText); layout->addWidget(intended);
    auto* title=new QLineEdit(name,&dialog); title->setObjectName("targetPageTitle"); title->setAccessibleName("New page title"); layout->addWidget(title);
    auto* database=new QComboBox(&dialog); database->setObjectName("targetDatabase"); database->setAccessibleName("Destination database for new page");
    for (const auto& db:workspace_.databases()) database->addItem(q(db.name)+" ["+q(db.id)+"]",q(db.id));
    auto selected=database->findData(scope); if (selected<0) selected=database->findData(q(session_.record().database_id));
    database->setCurrentIndex(selected); layout->addWidget(new QLabel("Destination database",&dialog)); layout->addWidget(database);
    auto* choose=new QPushButton("Choose existing target…",&dialog); choose->setObjectName("chooseReferenceTarget"); layout->addWidget(choose);
    auto* create=new QPushButton("Create page and link",&dialog); create->setObjectName("createReferenceTarget"); layout->addWidget(create);
    auto* cancel=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog); layout->addWidget(cancel);
    connect(cancel,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    enum class Action { None, Choose, Create }; Action action=Action::None;
    auto valid=[&]{ create->setEnabled(database->currentIndex()>=0 && !title->text().trimmed().isEmpty()); };
    connect(title,&QLineEdit::textChanged,&dialog,valid); valid();
    connect(choose,&QPushButton::clicked,&dialog,[&]{action=Action::Choose; dialog.accept();});
    connect(create,&QPushButton::clicked,&dialog,[&]{action=Action::Create; dialog.accept();});
    if (dialog.exec()!=QDialog::Accepted) { activateWindow(); editor->setFocus(); return; }
    activateWindow(); editor->setFocus();
    if (action==Action::Choose) {
        insert_link(name,reference->custom_label?old_label:QString{}); flush(); return;
    }
    if (action!=Action::Create) return;
    auto source=session_.record(); source.document=editor->snapshot(); source.title=s(title_->text());
    const auto target=workspace_.create_reference_target(source,*location,s(database->currentData().toString()),s(title->text().trimmed()));
    const Reference resolved{ReferenceKind::Page,target.document.id,"","",reference->custom_label};
    session_.adopt_saved(std::move(source)); loading_=true; link_targets_.invalidate();
    editor->replace_selected_reference(resolved,reference->custom_label?old_label:q(target.title));
    loading_=false; pending_=false; save_state_->setText("Saved locally"); refresh_table(); update_links();
    statusBar()->showMessage("Page created and reference saved locally. Undo restores the unresolved reference and keeps the created page.",8000);
}
void WorkspaceWindow::follow_reference(ReferenceKind kind, const std::string& id)
{
    if (!flush()) return;
    try {
        const auto target=workspace_.resolve_reference({kind,id});
        switch (target.state) {
        case ReferenceState::Available: open_page(target.page_id,target.block_id); return;
        case ReferenceState::PageTrashed: statusBar()->showMessage("This link's page is in trash. Use Restore… to restore it.",8000); return;
        case ReferenceState::DatabaseTrashed: statusBar()->showMessage("This link's database is in trash. Use Restore… to restore it.",8000); return;
        default: statusBar()->showMessage("This link target is unavailable or deleted. Deleted blocks may be recovered with Restore….",8000); return;
        }
    } catch (const std::exception& e) { statusBar()->showMessage(q(e.what()),8000); }
}
void WorkspaceWindow::update_transfer_actions()
{
    findChild<QAction*>("movePage")->setEnabled(session_.has_page());
    findChild<QAction*>("moveBlockToPage")->setEnabled(session_.has_page());
    findChild<QAction*>("undoBlockTransfer")->setEnabled(last_transfer_ && last_transfer_->applied);
    findChild<QAction*>("redoBlockTransfer")->setEnabled(last_transfer_ && !last_transfer_->applied);
    update_attachment_actions();
    update_trash_actions();
}
void WorkspaceWindow::update_attachment_actions()
{
    for (const auto* name:{"importImage","importFile"}) findChild<QAction*>(name)->setEnabled(session_.has_page());
    const bool active=session_.has_page() && !page_editor()->current_attachment_id().empty();
    for (const auto* name:{"openAttachment","exportAttachment"}) findChild<QAction*>(name)->setEnabled(active);
}
void WorkspaceWindow::show_search()
{
    if (!flush()) return;
    QDialog dialog(this); dialog.setObjectName("workspaceSearch"); dialog.setWindowTitle("Find in workspace"); auto* layout=new QVBoxLayout(&dialog);
    auto* query=new QLineEdit(last_search_,&dialog); query->setObjectName("searchText"); query->setMaxLength(1024); query->setAccessibleName("Search words");
    query->setPlaceholderText("Words to find; add * to a term for prefix matching"); layout->addWidget(query);
    auto* scope=new QComboBox(&dialog); scope->setObjectName("searchDatabase"); scope->setAccessibleName("Search database"); scope->addItem("All databases",QString{});
    for (const auto& db:workspace_.databases())
        scope->addItem(q(db.name)+" ["+q(db.id.substr(0,8))+"]",q(db.id));
    layout->addWidget(scope);
    auto* references=link_filter_controls(&dialog,workspace_,last_search_filters_); layout->addWidget(references);
    auto* reference_table=references->findChild<QTableWidget*>("linkFilters");
    auto* results=new QTreeWidget(&dialog); results->setObjectName("searchResults"); results->setHeaderLabels({"Page / database","Context"}); results->setRootIsDecorated(false); layout->addWidget(results);
    auto* state=new QLabel(&dialog); state->setTextFormat(Qt::PlainText); layout->addWidget(state);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons); buttons->button(QDialogButtonBox::Open)->setEnabled(false);
    QTimer debounce; debounce.setSingleShot(true); debounce.setInterval(150);
    auto run=[&]{
        results->clear(); buttons->button(QDialogButtonBox::Open)->setEnabled(false);
        try {
            const auto selected=selected_link_filters(reference_table);
            const auto matches=workspace_.search(s(query->text()),s(scope->currentData().toString()),selected);
            for (const auto& match:matches) {
                auto* item=new QTreeWidgetItem(results,{q(match.title)+" / "+q(match.database_name)+" ["+q(match.page_id.substr(0,8))+"]",q(match.context).simplified().left(240)});
                item->setData(0,Qt::UserRole,q(match.page_id)); item->setData(0,Qt::UserRole+1,q(match.block_id));
            }
            state->setText(query->text().trimmed().isEmpty() && selected.empty()?"Enter words or add a reference filter.":QString::number(matches.size())+" matching notes (up to 100). Select a result to open it.");
        } catch(const std::exception& e){state->setText(q(e.what()));}
    };
    connect(query,&QLineEdit::textChanged,&dialog,[&]{ debounce.start(); }); connect(&debounce,&QTimer::timeout,&dialog,run);
    connect(scope,&QComboBox::currentIndexChanged,&dialog,[&]{ debounce.stop(); run(); });
    connect(reference_table,&QTableWidget::cellChanged,&dialog,[&]{debounce.stop(); run();});
    connect(results,&QTreeWidget::currentItemChanged,&dialog,[&](QTreeWidgetItem* item){buttons->button(QDialogButtonBox::Open)->setEnabled(item);});
    connect(results,&QTreeWidget::itemActivated,&dialog,[&]{ buttons->button(QDialogButtonBox::Open)->click(); });
    std::string page,block;
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{ if (auto* item=results->currentItem()) { page=s(item->data(0,Qt::UserRole).toString()); block=s(item->data(0,Qt::UserRole+1).toString()); dialog.accept(); } });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    run(); query->selectAll(); query->setFocus(); dialog.resize(950,700); dialog.exec(); last_search_=query->text();
    try {last_search_filters_=selected_link_filters(reference_table);} catch(const std::invalid_argument&) {}
    activateWindow(); page_editor()->setFocus(); if (!page.empty()) open_page(page,block);
}
void WorkspaceWindow::markdown_interchange(bool importing)
{
    if (!flush()) return;
    if (importing && database_id_.empty()) throw std::invalid_argument("Select a destination database first");
    QFileDialog dialog(this,importing?"Import Markdown folder":"New Markdown export folder",q(workspace_.directory().u8string()));
    dialog.setObjectName(importing?"markdownImport":"markdownExport"); dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(importing?QFileDialog::Directory:QFileDialog::AnyFile);
    if (!importing) {dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.selectFile("markdown-export");}
    const auto accepted=dialog.exec()==QDialog::Accepted; activateWindow(); page_editor()->setFocus();
    if (!accepted || dialog.selectedFiles().isEmpty()) return;
    const auto folder=std::filesystem::u8path(s(dialog.selectedFiles()[0]));
    const auto report=run_interchange(workspace_,importing?InterchangeAction::ImportMarkdown:InterchangeAction::ExportMarkdown,
        importing?folder:std::filesystem::path{},importing?std::filesystem::path{}:folder,database_id_).report;
    if (importing) {link_targets_.invalidate(); refresh_table(); if (!report.pages.empty()) open_page(report.pages.front());}
    QString message=QString::number(report.pages.size())+(importing?" pages imported.":" pages exported.");
    for (const auto& warning:report.warnings) message+="\n\n"+q(warning);
    notify(this,QMessageBox::Information,importing?"Markdown import report":"Markdown export report",message);
    activateWindow(); page_editor()->setFocus();
}
void WorkspaceWindow::json_interchange(bool importing)
{
    if (!flush()) return;
    QString source;
    if (importing) {
        QFileDialog dialog(this,"Import lossless JSON folder",QDir("/imports").exists()?"/imports":q(workspace_.directory().u8string()));
        dialog.setObjectName("jsonImportSource"); dialog.setOption(QFileDialog::DontUseNativeDialog); dialog.setFileMode(QFileDialog::Directory);
        if (dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
        source=dialog.selectedFiles()[0];
    }
    QFileDialog dialog(this,importing?"New imported workspace folder":"New lossless JSON export folder",q(workspace_.directory().u8string()));
    dialog.setObjectName(importing?"jsonImportDestination":"jsonExport"); dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile); dialog.selectFile(importing?"imported-workspace":"json-export");
    const auto accepted=dialog.exec()==QDialog::Accepted; activateWindow(); page_editor()->setFocus();
    if (!accepted || dialog.selectedFiles().isEmpty()) return;
    const auto destination=std::filesystem::u8path(s(dialog.selectedFiles()[0]));
    if (importing) {
        auto result=run_interchange(workspace_,InterchangeAction::ImportJson,std::filesystem::u8path(s(source)),destination);
        auto workspace=std::shared_ptr<Workspace>(std::move(result.imported_workspace));
        auto* window=new WorkspaceWindow(*workspace); window->setAttribute(Qt::WA_DeleteOnClose);
        // Keep the imported workspace alive through destruction of its window.
        connect(window,&QObject::destroyed,qApp,[workspace]{(void)workspace;}); window->show();
    } else {
        run_interchange(workspace_,InterchangeAction::ExportJson,{},destination);
        notify(this,QMessageBox::Information,"Lossless JSON export","Exported notes, properties, views, trash, recovery history and retained attachment files to:\n"+q(destination.u8string()));
        activateWindow(); page_editor()->setFocus();
    }
}
void WorkspaceWindow::import_attachment(bool image)
{
    if (!session_.has_page() || !flush()) return;
    const auto after=s(page_editor()->current_block_id());
    QFileDialog dialog(this,image?"Import image":"Import file",QDir("/imports").exists()?"/imports":QDir::homePath());
    dialog.setObjectName("attachmentImport"); dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setOption(QFileDialog::DontResolveSymlinks);
    dialog.setFileMode(QFileDialog::ExistingFile);
    if (image) dialog.setNameFilter("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp);;All files (*)");
    const bool accepted=dialog.exec()==QDialog::Accepted; activateWindow(); page_editor()->setFocus();
    if (!accepted || dialog.selectedFiles().isEmpty()) return;
    const auto source_path=dialog.selectedFiles().first();
    const auto selected_path=std::filesystem::u8path(s(source_path));
    if (std::filesystem::is_symlink(selected_path) || !std::filesystem::is_regular_file(selected_path))
        throw std::invalid_argument("Choose a regular file; symlinks and special files cannot be imported");
    if (image) { QImageReader reader(source_path); reader.setDecideFormatFromContent(true); if (!reader.canRead()) throw std::invalid_argument("This file is not a supported image"); }
    auto page=session_.record(); page.document=page_editor()->snapshot();
    // Copy and hash on a worker thread with progress/cancel; the database write
    // happens afterwards, so no transaction is held while large files copy.
    auto staged=workspace_.prepare_attachment(selected_path,image);
    QProgressDialog progress("Copying "+q(selected_path.filename().u8string())+" into the workspace…","Cancel",0,1000,this);
    progress.setObjectName("attachmentProgress"); progress.setWindowModality(Qt::WindowModal); progress.setMinimumDuration(400); progress.setAutoClose(false);
    std::atomic<bool> cancel{false}; std::atomic<std::int64_t> copied{0},total{1}; std::exception_ptr failure;
    QEventLoop loop;
    std::thread worker([&]{
        try { staged.copy([&](std::int64_t done,std::int64_t size){ copied=done; total=std::max<std::int64_t>(size,1); return !cancel.load(); }); }
        catch (...) { failure=std::current_exception(); }
        QMetaObject::invokeMethod(&loop,&QEventLoop::quit,Qt::QueuedConnection);
    });
    QTimer tick; connect(&tick,&QTimer::timeout,&progress,[&]{
        progress.setValue(int(std::min<std::int64_t>(1000,copied*1000/total))); if (progress.wasCanceled()) cancel=true;
    });
    tick.start(50); loop.exec(); worker.join(); tick.stop(); progress.reset();
    activateWindow(); page_editor()->setFocus();
    if (failure) std::rethrow_exception(failure);
    const auto imported=workspace_.commit_attachment(page,after,staged);
    session_.adopt_saved(std::move(page)); loading_=true;
    page_editor()->apply_document(session_.record().document,q(imported.following));
    loading_=false; pending_=false; save_state_->setText("Saved locally"); link_targets_.invalidate(); refresh_table(); update_links(); update_attachment_actions();
    statusBar()->showMessage("Imported and saved a managed copy of "+q(imported.file.name),8000);
}
void WorkspaceWindow::export_attachment()
{
    const auto id=page_editor()->current_attachment_id(); const auto info=workspace_.attachment(id);
    if (!info) throw std::runtime_error("Attachment metadata is unavailable");
    const auto source=workspace_.attachment_path(id);
    QFileDialog dialog(this,"Save a copy of attachment",q(workspace_.directory().u8string()));
    dialog.setObjectName("attachmentExport"); dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.selectFile(q(info->name));
    const bool accepted=dialog.exec()==QDialog::Accepted; activateWindow(); page_editor()->setFocus();
    if (!accepted || dialog.selectedFiles().isEmpty()) return;
    const auto destination=std::filesystem::weakly_canonical(std::filesystem::u8path(s(dialog.selectedFiles().first())));
    const auto relative=destination.lexically_relative(workspace_.directory()/"attachments");
    if (relative.empty() || *relative.begin()!="..") throw std::invalid_argument("Save copies outside the managed attachments directory");
    if (destination.parent_path()==workspace_.directory()) {
        const auto name=destination.filename().u8string();
        if (name.rfind("nebula.sqlite3",0)==0 || name==".nebula.lock" || name=="backup-manifest.json")
            throw std::invalid_argument("Choose a destination other than a workspace storage file");
    }
    QFile input(q(source.u8string())); QSaveFile output(q(destination.u8string()));
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot open the attachment or export destination");
    QCryptographicHash hash(QCryptographicHash::Sha256); std::int64_t size=0;
    while (!input.atEnd()) {
        const auto bytes=input.read(65536);
        if (bytes.isEmpty() && input.error()!=QFile::NoError) throw std::runtime_error("Cannot read the managed attachment");
        hash.addData(bytes); size+=bytes.size();
        if (output.write(bytes)!=bytes.size()) throw std::runtime_error("Cannot write the attachment copy");
    }
    if (size!=info->size || hash.result().toHex().toStdString()!=info->sha256) throw std::runtime_error("Managed attachment bytes differ from their import checksum");
    if (!output.commit()) throw std::runtime_error("Cannot persist the attachment copy");
    statusBar()->showMessage("Attachment copy saved.",6000);
}
void WorkspaceWindow::show_attachment_audit()
{
    if (!flush()) return;
    QDialog dialog(this); dialog.setObjectName("attachmentAudit"); dialog.setWindowTitle("Managed files"); auto* layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Registered imports remain retained for notes, undo and recovery. Cleanup removes only unregistered leftovers; backups keep independent copies.",&dialog));
    auto* list=new QListWidget(&dialog); list->setObjectName("attachmentIssues"); layout->addWidget(list);
    auto* state=new QLabel(&dialog); state->setWordWrap(true); state->setTextFormat(Qt::PlainText); layout->addWidget(state);
    auto* refresh=new QPushButton("Check files",&dialog); layout->addWidget(refresh);
    auto* remove=new QPushButton("Delete selected unregistered file permanently",&dialog); remove->setObjectName("removeUnusedAttachment"); remove->setEnabled(false); layout->addWidget(remove);
    auto reload=[&]{
        list->clear(); const auto issues=workspace_.audit_attachments();
        for (const auto& issue:issues) {
            auto* item=new QListWidgetItem(q(issue.message)+" — "+q(issue.path.empty()?issue.attachment_id:issue.path),list);
            item->setData(Qt::UserRole,q(issue.path)); item->setData(Qt::UserRole+1,issue.removable);
        }
        state->setText(issues.empty()?"All managed attachment files are available and match their checksums.":QString::number(issues.size())+" file notices found. Registered imports and referenced files remain retained.");
        remove->setEnabled(false);
    };
    connect(refresh,&QPushButton::clicked,&dialog,[&]{try {reload();} catch(const std::exception& e){state->setText(q(e.what()));}});
    connect(list,&QListWidget::currentItemChanged,&dialog,[&](QListWidgetItem* item){remove->setEnabled(item && item->data(Qt::UserRole+1).toBool());});
    connect(remove,&QPushButton::clicked,&dialog,[&]{
        if (auto* item=list->currentItem()) try { workspace_.remove_unused_attachment(s(item->data(Qt::UserRole).toString())); reload(); }
            catch(const std::exception& e) {state->setText(q(e.what()));}
    });
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(close); connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::accept);
    reload(); dialog.resize(800,450); dialog.exec(); activateWindow(); page_editor()->setFocus();
}
void WorkspaceWindow::reload_page(const std::string& block)
{
    if (!session_.has_page()) return;
    const auto id=session_.record().document.id;
    // These operations have committed outside the page editor. Reload its revision
    // and clear local undo so it cannot recreate transferred block identities.
    history_navigation_=true; session_.close(); open_page(id,block); history_navigation_=false;
}
void WorkspaceWindow::move_page()
{
    if (!session_.has_page() || !flush()) return;
    const auto block=s(page_editor()->current_block_id());
    QDialog dialog(this); dialog.setWindowTitle("Move page to database");
    auto* layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Property values follow the page. Matching names must have the same type; missing properties are added.",&dialog));
    auto* choices=new QComboBox(&dialog); choices->setObjectName("moveDestinationDatabase"); choices->setAccessibleName("Destination database");
    for (const auto& db:workspace_.databases()) if (db.id!=session_.record().database_id)
        choices->addItem(q(db.name)+" ["+q(db.id.substr(0,8))+"]",q(db.id));
    layout->addWidget(choices);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(choices->count()>0);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const bool accepted=dialog.exec()==QDialog::Accepted;
    activateWindow(); page_editor()->setFocus();
    if (!accepted) return;
    auto page=session_.record(); workspace_.move_page(page,s(choices->currentData().toString()));
    link_targets_.invalidate(); reload_page(block);
    statusBar()->showMessage("Page moved and saved locally.",6000);
}
void WorkspaceWindow::move_block()
{
    if (!session_.has_page() || !flush()) return;
    const auto block=s(page_editor()->current_block_id());
    QDialog dialog(this); dialog.setWindowTitle("Move block to page");
    auto* layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Move the active block and its children to the end of another page. Links keep their original targets.",&dialog));
    auto* choices=new QComboBox(&dialog); choices->setObjectName("moveDestinationPage"); choices->setAccessibleName("Destination page");
    for (const auto& page:workspace_.page_summaries())
        if (page.id!=session_.record().document.id)
            choices->addItem(q(page.database_name)+" / "+q(page.title)+" ["+q(page.id.substr(0,8))+"]",q(page.id));
    layout->addWidget(choices);
    layout->addWidget(new QLabel("This saves both pages and clears this page's editing undo. Workspace → Undo block transfer reverses the latest transfer while both pages are unchanged.",&dialog));
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(choices->count()>0);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const bool accepted=dialog.exec()==QDialog::Accepted;
    activateWindow(); page_editor()->setFocus();
    if (!accepted) return;
    const auto destination=workspace_.load_page(s(choices->currentData().toString()));
    auto transfer=workspace_.transfer_blocks(session_.record(),destination,block);
    last_transfer_=std::move(transfer); transferred_block_=block;
    link_targets_.invalidate(); reload_page(); update_transfer_actions();
    statusBar()->showMessage("Block and children moved; both pages saved locally.",6000);
}
void WorkspaceWindow::undo_block_transfer(bool applied)
{
    if (!last_transfer_ || !flush()) return;
    workspace_.set_transfer_applied(*last_transfer_,applied); link_targets_.invalidate();
    if (session_.has_page() && (session_.record().document.id==last_transfer_->source_before.id
        || session_.record().document.id==last_transfer_->destination_before.id))
        reload_page(applied?std::string{}:transferred_block_);
    update_transfer_actions();
    statusBar()->showMessage(applied?"Block transfer redone and saved locally.":"Block transfer undone and saved locally.",6000);
}
void WorkspaceWindow::closeEvent(QCloseEvent* event)
{
    if (flush()) { event->accept(); return; }
    QMessageBox box(QMessageBox::Warning,"Unsaved changes",session_.conflicted()
        ?"This page changed elsewhere, so your edits could not be saved. Keep them as a new page, keep this window open, or discard them?"
        :"Saving failed. Keep this window open to retry, or discard its pending changes?",QMessageBox::Discard|QMessageBox::Cancel,this);
    box.setObjectName("unsavedChanges"); box.setTextFormat(Qt::PlainText); box.setDefaultButton(QMessageBox::Cancel);
    QPushButton* keep=session_.conflicted()?box.addButton("Keep as new page",QMessageBox::AcceptRole):nullptr;
    if (keep) keep->setObjectName("keepConflictCopy");
    box.exec();
    if (keep && box.clickedButton()==keep) {
        try { save_conflict_copy(); event->accept(); } catch (const std::exception& e) { error(this,e); event->ignore(); }
        return;
    }
    if (box.clickedButton()==box.button(QMessageBox::Discard)) { session_.discard(); pending_=false; event->accept(); } else event->ignore();
}
WorkspaceWindow::~WorkspaceWindow()
{
    if (!open_copies_.empty()) { std::error_code ignored; std::filesystem::remove_all(open_copies_,ignored); }
}
void WorkspaceWindow::save_conflict_copy()
{
    if (!session_.has_page()) return;
    autosave_->stop();
    auto pending=session_.record();
    if (pending_) { pending.document=page_editor()->snapshot(); pending.title=s(title_->text()); }
    // Fresh identities keep the copy independent; links between its blocks follow.
    const auto copy=with_fresh_ids(pending.document,"conflict-copy",[]{ return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); });
    std::map<std::string,PropertyValue> values;
    const auto definitions=workspace_.properties(pending.database_id);
    for (const auto& entry:pending.properties)
        if (std::any_of(definitions.begin(),definitions.end(),[&](const auto& d){return d.id==entry.first;})) values.insert(entry);
    const auto created=workspace_.create_page(pending.database_id,pending.title+" (conflicting copy)",values,copy.blocks);
    session_.discard(); pending_=false; link_targets_.invalidate();
    const auto original=pending.document.id;
    history_navigation_=true; session_.close(); history_navigation_=false;
    open_page(created.document.id);
    findChild<QAction*>("saveConflictCopy")->setEnabled(false);
    statusBar()->showMessage("Your edits were saved as a new page; the other version of \""+q(pending.title)+"\" was kept.",10000);
    Q_UNUSED(original);
}
void WorkspaceWindow::empty_trash(QWidget* parent)
{
    if (!flush()) throw std::runtime_error("Save or discard pending edits before emptying the trash");
    std::size_t pages=0,databases=0;
    for (const auto& page:workspace_.page_summaries(true)) if (page.trashed || page.database_trashed) ++pages;
    for (const auto& db:workspace_.databases(true)) if (db.trashed) ++databases;
    const auto fragments=workspace_.deleted_blocks().size();
    if (!confirm(parent,"confirmEmptyTrash","Empty trash permanently",
        QString("Permanently delete %1 trashed pages, %2 trashed databases and %3 deleted-block recovery records, together with imported files nothing else uses?\n\n"
                "This cannot be undone. Existing backups and exports keep their own copies.").arg(pages).arg(databases).arg(fragments))) return;
    // Editor and block-transfer undo could otherwise restore purged identities or files.
    const auto open=session_.has_page()?session_.record().document.id:std::string{};
    last_transfer_.reset(); update_transfer_actions();
    history_navigation_=true; session_.close(); history_navigation_=false;
    const auto report=workspace_.empty_trash();
    link_targets_.invalidate(); refresh_databases();
    if (!open.empty()) open_page(open);
    statusBar()->showMessage(QString("Permanently deleted %1 pages, %2 databases, %3 recovery records and %4 files%5.")
        .arg(report.pages).arg(report.databases).arg(report.fragments).arg(report.files).arg(report.compacted?"; the database file was compacted":""),10000);
}
void WorkspaceWindow::delete_view()
{
    if (!flush() || current_view_.id.empty()) return;
    if (!confirm(this,"confirmDeleteView","Delete view","Delete the saved view \""+q(current_view_.name)+"\"? Pages are not affected.")) return;
    workspace_.delete_view(current_view_.id); switch_database();
}
void WorkspaceWindow::rename_property()
{
    if (!flush() || definitions_.empty()) return;
    QStringList names; for (const auto& d:definitions_) names<<q(d.name);
    bool ok=false; const auto chosen=QInputDialog::getItem(this,"Rename property","Property",names,0,false,&ok); if (!ok) return;
    const auto& definition=definitions_[std::size_t(names.indexOf(chosen))];
    const auto name=QInputDialog::getText(this,"Rename property","New name",QLineEdit::Normal,chosen,&ok);
    if (!ok || name.trimmed().isEmpty()) return;
    workspace_.rename_property(definition.id,s(name.trimmed())); refresh_table();
}
void WorkspaceWindow::delete_property()
{
    if (!flush() || definitions_.empty()) return;
    QStringList names; for (const auto& d:definitions_) names<<q(d.name);
    bool ok=false; const auto chosen=QInputDialog::getItem(this,"Delete property","Property",names,0,false,&ok); if (!ok) return;
    const auto definition=definitions_[std::size_t(names.indexOf(chosen))];
    if (!confirm(this,"confirmDeleteProperty","Delete property","Delete \""+chosen+"\" and its value on every page of this database? Views stop using it. This cannot be undone.")) return;
    const auto open=session_.has_page()?session_.record().document.id:std::string{};
    history_navigation_=true; session_.close(); history_navigation_=false;
    workspace_.delete_property(definition.id); switch_database();
    if (!open.empty()) open_page(open);
}
void WorkspaceWindow::check_integrity()
{
    if (!flush()) return;
    const auto report=workspace_.check_integrity(); QString text;
    if (report.ok()) text="The workspace database passed its integrity and relationship checks.";
    else { text="Problems were found. Back up the folder and keep this report:\n"; for (const auto& problem:report.problems) text+="\n• "+q(problem); }
    notify(this,report.ok()?QMessageBox::Information:QMessageBox::Warning,"Workspace integrity",text);
}
void WorkspaceWindow::open_attachment()
{
    const auto id=page_editor()->current_attachment_id(); const auto info=workspace_.attachment(id);
    if (!info) throw std::runtime_error("Attachment metadata is unavailable");
    const auto source=workspace_.attachment_path(id);
    // External programs get a read-only copy named like the original, never the
    // managed file itself: an in-place save would break its recorded checksum.
    if (open_copies_.empty()) {
        open_copies_=std::filesystem::u8path(s(QStandardPaths::writableLocation(QStandardPaths::TempLocation)))
            /("nebula-open-"+std::to_string(::getpid())+"-"+QUuid::createUuid().toString(QUuid::Id128).toStdString());
        files::make_private_directory(open_copies_);
    }
    const auto folder=open_copies_/(id+"-"+QUuid::createUuid().toString(QUuid::Id128).left(8).toStdString());
    files::make_private_directory(folder);
    const auto copy=folder/std::filesystem::u8path(s(copy_name(info->name)));
    const auto digest=files::copy_new(source,copy);
    if (digest.size!=info->size || digest.sha256!=info->sha256) { std::error_code ignored; std::filesystem::remove(copy,ignored);
        throw std::runtime_error("Managed attachment bytes differ from their import checksum"); }
    files::make_read_only(copy);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(q(copy.u8string())))) throw std::runtime_error("No desktop application could open this file; use Save a copy…");
}
} // namespace nebula
