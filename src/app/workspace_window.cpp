#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/core/wiki.hpp"
#include <QAction>
#include <QAbstractItemView>
#include <QCompleter>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
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
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace nebula {
namespace {
QString q(const std::string& text) { return QString::fromUtf8(text.data(),qsizetype(text.size())); }
std::string s(const QString& text) { return text.toUtf8().toStdString(); }
QString display(const PropertyValue& value) {
    return std::visit([](const auto& v) -> QString {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::monostate>) return {};
        else if constexpr (std::is_same_v<T,std::string>) return q(v);
        else if constexpr (std::is_same_v<T,Date>) return q(v.iso);
        else if constexpr (std::is_same_v<T,double>) return QString::number(v,'g',16);
        else if constexpr (std::is_same_v<T,bool>) return v ? "true" : "false";
        else { QStringList tags; for (const auto& tag:v) tags<<q(tag); return tags.join(", "); }
    },value);
}
PropertyValue parse_value(PropertyKind kind, const QString& text) {
    if (text.trimmed().isEmpty()) return {};
    PropertyValue value;
    switch (kind) {
    case PropertyKind::Text: value=s(text); break;
    case PropertyKind::Number: { bool ok=false; const double n=text.toDouble(&ok); if (!ok) throw std::invalid_argument("Enter a number"); value=n; break; }
    case PropertyKind::Date: value=Date{s(text.trimmed())}; break;
    case PropertyKind::Checkbox:
        if (text.trimmed()=="true" || text.trimmed()=="1") value=true;
        else if (text.trimmed()=="false" || text.trimmed()=="0") value=false;
        else throw std::invalid_argument("Enter true or false for a checkbox");
        break;
    case PropertyKind::Tags: {
        std::vector<std::string> tags;
        for (const auto& tag:text.split(',')) tags.push_back(s(tag.trimmed()));
        value=std::move(tags); break;
    }
    }
    validate_value(kind,value); return value;
}
void error(QWidget* parent, const std::exception& e) { QMessageBox::warning(parent,"Nebula",q(e.what())); }
} // namespace

WorkspaceWindow::WorkspaceWindow(Workspace& workspace, const QString& display_path, QWidget* parent)
    : PrototypeWindow(Page{"no-page",{{"placeholder","",BlockKind::Paragraph,{}}}},
          display_path.isEmpty()?q(workspace.directory().u8string()):display_path,false,parent),
      workspace_(workspace),session_(workspace)
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
    page_layout->insertWidget(1,title_); page_layout->insertWidget(2,save_state_);
    backlinks_=new QListWidget(page_panel); backlinks_->setObjectName("backlinks"); backlinks_->setMaximumHeight(90);
    page_layout->addWidget(new QLabel("Backlinks — pages that link here",page_panel)); page_layout->addWidget(backlinks_);
    auto* navigation=new QWidget;
    auto* navigation_layout=new QVBoxLayout(navigation);
    databases_=new QComboBox(navigation); databases_->setObjectName("databases"); databases_->setAccessibleName("Database");
    views_=new QComboBox(navigation); views_->setObjectName("savedViews"); views_->setAccessibleName("Saved view");
    navigation_layout->addWidget(databases_); navigation_layout->addWidget(views_);
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
    addToolBarBreak();
    auto* bar=addToolBar("Database controls"); bar->setObjectName("databaseControls"); bar->setMovable(false);
    auto add=[&](const char* text,const char* name,auto callback) {
        auto* action=bar->addAction(text); action->setObjectName(name);
        connect(action,&QAction::triggered,this,[this,callback]{ try { callback(); } catch(const std::exception& e) { error(this,e); } });
    };
    add("New database","newDatabase",[this]{
        if (!flush()) return;
        bool ok=false; const auto name=QInputDialog::getText(this,"New database","Name",QLineEdit::Normal,"Notes",&ok);
        if (ok && !name.trimmed().isEmpty()) { auto db=workspace_.create_database(s(name.trimmed())); refresh_databases(); databases_->setCurrentIndex(databases_->findData(q(db.id))); }
    });
    add("Rename database","renameDatabase",[this]{
        bool ok=false; const auto name=QInputDialog::getText(this,"Rename database","Name",QLineEdit::Normal,databases_->currentText(),&ok);
        if (ok && !name.trimmed().isEmpty()) { workspace_.rename_database(database_id_,s(name.trimmed())); refresh_databases(); }
    });
    add("New page","newPage",[this]{
        bool ok=false; const auto title=QInputDialog::getText(this,"New page","Title",QLineEdit::Normal,"Untitled",&ok);
        if (ok) create_page(title);
    });
    add("Journal entry","newJournalEntry",[this]{
        if (!flush() || database_id_.empty()) return;
        bool ok=false; const auto date=QInputDialog::getText(this,"Journal entry","Entry date (YYYY-MM-DD)",QLineEdit::Normal,QDate::currentDate().toString(Qt::ISODate),&ok);
        if (!ok) return;
        if (!valid_date(s(date))) throw std::invalid_argument("Enter a valid YYYY-MM-DD date");
        auto found=std::find_if(definitions_.begin(),definitions_.end(),[](const auto& d){ return d.name=="Entry date" && d.kind==PropertyKind::Date; });
        std::string date_id;
        if (found==definitions_.end()) date_id=workspace_.add_property(database_id_,"Entry date",PropertyKind::Date).id;
        else date_id=found->id;
        auto page=workspace_.create_page(database_id_,s(date),{{date_id,Date{s(date)}}});
        refresh_table(); open_page(page.document.id);
    });
    add("Add property","addProperty",[this]{ add_property(); });
    add("New view","newView",[this]{ edit_view(true); });
    add("Edit view","editView",[this]{ edit_view(false); });
    add("Link…","insertLink",[this]{ insert_link(); });
    add("Trash page","trashPage",[this]{
        if (!session_.has_page() || !flush()) return;
        const auto id=session_.record().document.id; workspace_.trash_page(id,true); session_.close(); switch_database();
    });
    add("Trash database","trashDatabase",[this]{ if (flush() && !database_id_.empty()) { workspace_.trash_database(database_id_,true); session_.close(); refresh_databases(); } });
    add("Restore…","restoreTrash",[this]{ show_trash(); });
    autosave_=new QTimer(this); autosave_->setSingleShot(true); autosave_->setInterval(500);
    connect(autosave_,&QTimer::timeout,this,[this]{ flush(); });
    connect(page_editor(),&QTextEdit::textChanged,this,&WorkspaceWindow::mark_pending);
    connect(title_,&QLineEdit::textEdited,this,&WorkspaceWindow::mark_pending);
    connect(databases_,&QComboBox::currentIndexChanged,this,[this]{ if (!loading_) switch_database(); });
    connect(views_,&QComboBox::currentIndexChanged,this,[this](int i){ if (!loading_ && i>=0 && i<int(saved_views_.size())) { current_view_=saved_views_[std::size_t(i)]; refresh_table(); } });
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{
        if (!loading_ && table_->currentRow()>=0) open_page(s(table_->item(table_->currentRow(),0)->data(Qt::UserRole).toString()));
    });
    connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if (loading_) return;
        try {
            const auto id=s(table_->item(item->row(),0)->data(Qt::UserRole).toString());
            const int column=item->column();
            const auto text=item->text();
            const auto property_id=column?s(table_->horizontalHeaderItem(column)->data(Qt::UserRole).toString()):std::string{};
            if (!open_page(id)) return;
            if (column==0) { session_.set_title(s(text)); QSignalBlocker guard(title_); title_->setText(text); }
            else {
                const auto def=std::find_if(definitions_.begin(),definitions_.end(),[&](const auto& d){return d.id==property_id;});
                if (def!=definitions_.end()) session_.set_property(property_id,parse_value(def->kind,text));
            }
            flush();
        } catch (const std::exception& e) { error(this,e); refresh_table(); }
    });
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
            ReferenceKind(index.data(Qt::UserRole).toInt()),index.data(Qt::UserRole+1).toString());
        completing_=false;
    });
    connect(page_editor(),&PageEditor::unresolved_reference_activated,this,[this](const QString& name,const QString&){
        statusBar()->showMessage("Unresolved link: "+name+". Select its text and use Link… to choose a target.",8000);
    });
    connect(backlinks_,&QListWidget::itemActivated,this,[this](QListWidgetItem* item){ open_page(s(item->data(Qt::UserRole).toString()),s(item->data(Qt::UserRole+1).toString())); });
    if (workspace_.databases(true).empty()) {
        const auto db=workspace_.create_database("Journal");
        View all; all.database_id=db.id; all.name="All entries";
        all.visible_properties.push_back(workspace_.add_property(db.id,"Entry date",PropertyKind::Date).id);
        all.visible_properties.push_back(workspace_.add_property(db.id,"Mood",PropertyKind::Text).id);
        all.visible_properties.push_back(workspace_.add_property(db.id,"Tags",PropertyKind::Tags).id);
        workspace_.save_view(all);
    }
    refresh_databases();
}
void WorkspaceWindow::mark_pending()
{
    if (loading_ || !session_.has_page()) return;
    pending_=true; save_state_->setText("Unsaved changes…"); autosave_->start();
}
bool WorkspaceWindow::flush()
{
    if (loading_ || !session_.has_page()) return true;
    autosave_->stop();
    if (!pending_ && !session_.dirty()) return true;
    try {
        if (pending_) { session_.replace_document(page_editor()->snapshot()); session_.set_title(s(title_->text())); pending_=false; }
        if (!session_.save()) throw std::runtime_error(session_.error());
        save_state_->setText("Saved locally"); refresh_table(); update_links(); return true;
    } catch (const std::exception& e) {
        save_state_->setText("Save failed — "+q(e.what())+". Press Ctrl+S to retry."); return false;
    }
}
void WorkspaceWindow::refresh_databases()
{
    const auto previous=database_id_; loading_=true; databases_->clear();
    for (const auto& db:workspace_.databases()) databases_->addItem(q(db.name),q(db.id));
    const int row=databases_->findData(q(previous)); if (row>=0) databases_->setCurrentIndex(row);
    loading_=false; switch_database();
}
void WorkspaceWindow::switch_database()
{
    if (!flush()) { QSignalBlocker guard(databases_); databases_->setCurrentIndex(databases_->findData(q(database_id_))); return; }
    session_.close(); pending_=false; loading_=true;
    database_id_=s(databases_->currentData().toString());
    page_editor()->set_page({"no-page",{{"placeholder","",BlockKind::Paragraph,{}}}});
    page_editor()->setEnabled(false); title_->clear(); title_->setEnabled(false); backlinks_->clear();
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
    const auto rows=apply_view(workspace_.pages(database_id_),current_view_);
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
    loading_=old_loading;
}
bool WorkspaceWindow::open_page(const std::string& id, const std::string& block)
{
    try {
        if (session_.has_page() && session_.record().document.id==id) { if (!block.empty()) page_editor()->go_to_block(q(block)); return true; }
        if (!flush()) return false;
        const auto target=workspace_.load_page(id);
        const int database_index=databases_->findData(q(target.database_id));
        if (database_index<0) throw std::runtime_error("This page's database is in trash");
        if (target.database_id!=database_id_) { databases_->setCurrentIndex(database_index); }
        session_.open(id); loading_=true;
        completing_=false;
        title_->setText(q(session_.record().title)); title_->setEnabled(true); page_editor()->set_page(session_.record().document); page_editor()->setEnabled(true);
        for (auto* bar:findChildren<QToolBar*>()) bar->setEnabled(true);
        loading_=false; pending_=false; save_state_->setText("Saved locally"); refresh_table(); update_links();
        update_transfer_actions();
        if (!block.empty()) page_editor()->go_to_block(q(block));
        page_editor()->setFocus(); return true;
    } catch (const std::exception& e) { save_state_->setText(q(e.what())); return false; }
}
void WorkspaceWindow::create_page(const QString& title)
{
    if (!flush() || database_id_.empty()) return;
    const auto page=workspace_.create_page(database_id_,s(title)); refresh_table(); open_page(page.document.id);
}
void WorkspaceWindow::update_links()
{
    backlinks_->clear(); if (!session_.has_page()) return;
    for (const auto& edge:workspace_.backlinks(ReferenceKind::Page,session_.record().document.id)) {
        auto* item=new QListWidgetItem(q(edge.source_title),backlinks_); item->setData(Qt::UserRole,q(edge.source_page)); item->setData(Qt::UserRole+1,q(edge.source_block));
    }
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
    auto* message=new QLabel(&dialog); layout->addWidget(message);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        try {
            View view; if (!create) view.id=current_view_.id; view.database_id=database_id_; view.name=s(name->text().trimmed()); view.sort_property=s(sort->currentData().toString()); view.ascending=ascending->isChecked();
            for (int i=0;i<visible->count();++i) if (visible->item(i)->checkState()==Qt::Checked) view.visible_properties.push_back(s(visible->item(i)->data(Qt::UserRole).toString()));
            for (int r=0;r<filters->rowCount();++r) {
                const auto id=s(qobject_cast<QComboBox*>(filters->cellWidget(r,0))->currentData().toString());
                const auto op=Comparison(qobject_cast<QComboBox*>(filters->cellWidget(r,1))->currentIndex());
                const auto text=qobject_cast<QLineEdit*>(filters->cellWidget(r,2))->text();
                auto kind=PropertyKind::Text; for (const auto& d:definitions_) if (d.id==id) kind=d.kind;
                auto value=(op==Comparison::Empty || op==Comparison::NotEmpty)?PropertyValue{}:parse_value(op==Comparison::Contains?PropertyKind::Text:kind,text);
                view.filters.push_back({id,op,value});
            }
            workspace_.save_view(view); current_view_=view; saved_views_=workspace_.views(database_id_);
            loading_=true; views_->clear(); int index=0;
            for (std::size_t i=0;i<saved_views_.size();++i) { views_->addItem(q(saved_views_[i].name)); if (saved_views_[i].id==view.id) index=int(i); }
            views_->setCurrentIndex(index); loading_=false; refresh_table(); dialog.accept();
        } catch (const std::exception& e) { message->setText(q(e.what())); }
    });
    dialog.exec();
}
void WorkspaceWindow::show_trash()
{
    if (!flush()) return;
    QDialog dialog(this); dialog.setWindowTitle("Restore from trash"); auto* layout=new QVBoxLayout(&dialog); auto* list=new QListWidget(&dialog); list->setObjectName("trashItems"); layout->addWidget(list);
    for (const auto& db:workspace_.databases(true)) {
        if (db.trashed) { auto* item=new QListWidgetItem("Database: "+q(db.name),list); item->setData(Qt::UserRole,q(db.id)); item->setData(Qt::UserRole+1,true); }
        for (const auto& page:workspace_.pages(db.id,true)) if (page.trashed) { auto* item=new QListWidgetItem(q(db.name)+" / "+q(page.title),list); item->setData(Qt::UserRole,q(page.document.id)); }
    }
    for (const auto& group:workspace_.deleted_blocks()) {
        auto* item=new QListWidgetItem("Blocks in "+q(group.page_title)+": "+q(group.label).left(60),list);
        item->setData(Qt::UserRole,q(group.id)); item->setData(Qt::UserRole+2,"blocks");
    }
    auto* restore=new QPushButton("Restore selected",&dialog); restore->setObjectName("restoreSelected"); layout->addWidget(restore);
    connect(restore,&QPushButton::clicked,&dialog,[&]{
        if (auto* item=list->currentItem()) {
            try {
                const auto id=s(item->data(Qt::UserRole).toString());
                if (item->data(Qt::UserRole+2).toString()=="blocks") workspace_.restore_deleted_blocks(id);
                else if (item->data(Qt::UserRole+1).toBool()) workspace_.trash_database(id,false);
                else workspace_.trash_page(id,false);
                delete item;
            } catch (const std::exception& e) { error(&dialog,e); }
        }
    });
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(close); connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::accept); dialog.exec(); refresh_databases();
}
std::vector<WorkspaceWindow::LinkCandidate> WorkspaceWindow::link_candidates() const
{
    std::vector<LinkCandidate> result;
    for (const auto& db:workspace_.databases()) for (auto page:workspace_.pages(db.id)) {
        if (session_.has_page() && page.document.id==session_.record().document.id) {
            page.document=page_editor()->snapshot(); page.title=s(title_->text());
        }
        result.push_back({ReferenceKind::Page,page.document.id,page.title,page.document.id,page.title,db.name,false});
        for (const auto& block:page.document.blocks) {
            std::string text; for (const auto& run:block.runs) text+=run.text;
            if (text.empty()) text=block.asset_label.empty()?"Empty block":block.asset_label;
            result.push_back({ReferenceKind::Block,block.id,text,page.document.id,page.title,db.name,block.kind==BlockKind::Heading});
        }
    }
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
        const auto& target=matches.front(); page_editor()->insert_reference(q(link->custom_label?link->label:target.label),target.kind,q(target.id));
    } else if (matches.empty() && link->heading.empty() && link->block.empty()) {
        page_editor()->insert_unresolved_reference(q(link->custom_label?link->label:link->page),q(link->page),q(database_id_));
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
    auto* tree=new QTreeWidget(&dialog); tree->setObjectName("linkTargets"); tree->setHeaderLabel("Choose a specific target"); layout->addWidget(tree);
    std::map<std::string,QTreeWidgetItem*> pages;
    for (const auto& target:link_candidates()) {
        const auto label=target.kind==ReferenceKind::Page?q(target.database)+" / "+q(target.title)
            :(target.heading?"Heading: ":"Block: ")+q(target.label).left(100);
        auto* item=target.kind==ReferenceKind::Page?new QTreeWidgetItem(tree):new QTreeWidgetItem(pages.at(target.page));
        item->setText(0,label+" ["+q(target.id)+"]"); item->setData(0,Qt::UserRole,q(target.id));
        item->setData(0,Qt::UserRole+1,int(target.kind)); item->setData(0,Qt::UserRole+2,q(target.label));
        if (target.kind==ReferenceKind::Page) pages.emplace(target.id,item);
    }
    auto* label=new QLineEdit(&dialog); label->setObjectName("linkLabel"); label->setText(custom_label); label->setPlaceholderText("Optional custom label"); layout->addWidget(label);
    auto* unresolved=new QLineEdit(&dialog); unresolved->setObjectName("unresolvedName"); unresolved->setPlaceholderText("Name of an unresolved page (does not create it)"); layout->addWidget(unresolved);
    auto* keep_unresolved=new QPushButton("Insert unresolved link",&dialog); keep_unresolved->setObjectName("insertUnresolved"); layout->addWidget(keep_unresolved);
    connect(keep_unresolved,&QPushButton::clicked,&dialog,[&]{
        if (!unresolved->text().trimmed().isEmpty()) {
            const auto name=unresolved->text().trimmed(); page_editor()->insert_unresolved_reference(label->text().isEmpty()?name:label->text(),name,q(database_id_)); dialog.accept();
        }
    });
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(tree,&QTreeWidget::currentItemChanged,&dialog,[&](QTreeWidgetItem* item){ buttons->button(QDialogButtonBox::Ok)->setEnabled(item && !item->isHidden()); });
    connect(search,&QLineEdit::textChanged,&dialog,[&](const QString& text){
        tree->setCurrentItem(nullptr);
        for (int p=0;p<tree->topLevelItemCount();++p) {
            auto* page=tree->topLevelItem(p); bool visible=page->text(0).contains(text,Qt::CaseInsensitive);
            for (int b=0;b<page->childCount();++b) {
                auto* block=page->child(b); const auto combined=page->text(0)+" "+block->text(0);
                const auto terms=text.split(' ',Qt::SkipEmptyParts);
                const bool match=std::all_of(terms.begin(),terms.end(),[&](const auto& term){return combined.contains(term,Qt::CaseInsensitive);});
                block->setHidden(!match); visible=visible||match;
            }
            page->setHidden(!visible); page->setExpanded(!text.isEmpty());
        }
    });
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{ if (auto* item=tree->currentItem(); item && !item->isHidden()) { page_editor()->insert_reference(label->text().isEmpty()?item->data(0,Qt::UserRole+2).toString():label->text(),ReferenceKind(item->data(0,Qt::UserRole+1).toInt()),item->data(0,Qt::UserRole).toString()); dialog.accept(); } });
    connect(tree,&QTreeWidget::itemDoubleClicked,&dialog,[&]{ buttons->button(QDialogButtonBox::Ok)->click(); });
    search->setText(query.trimmed()); dialog.resize(760,550); dialog.exec(); activateWindow(); page_editor()->setFocus();
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
}
void WorkspaceWindow::reload_page(const std::string& block)
{
    if (!session_.has_page()) return;
    const auto id=session_.record().document.id;
    // These operations have committed outside the page editor. Reload its revision
    // and clear local undo so it cannot recreate transferred block identities.
    session_.close(); open_page(id,block);
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
    reload_page(block);
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
    for (const auto& db:workspace_.databases()) for (const auto& page:workspace_.pages(db.id))
        if (page.document.id!=session_.record().document.id)
            choices->addItem(q(db.name)+" / "+q(page.title)+" ["+q(page.document.id.substr(0,8))+"]",q(page.document.id));
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
    reload_page(); update_transfer_actions();
    statusBar()->showMessage("Block and children moved; both pages saved locally.",6000);
}
void WorkspaceWindow::undo_block_transfer(bool applied)
{
    if (!last_transfer_ || !flush()) return;
    workspace_.set_transfer_applied(*last_transfer_,applied);
    if (session_.has_page() && (session_.record().document.id==last_transfer_->source_before.id
        || session_.record().document.id==last_transfer_->destination_before.id))
        reload_page(applied?std::string{}:transferred_block_);
    update_transfer_actions();
    statusBar()->showMessage(applied?"Block transfer redone and saved locally.":"Block transfer undone and saved locally.",6000);
}
void WorkspaceWindow::closeEvent(QCloseEvent* event)
{
    if (flush()) { event->accept(); return; }
    const auto answer=QMessageBox::warning(this,"Unsaved changes","Saving failed. Keep this window open to retry, or discard its pending changes?",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
    if (answer==QMessageBox::Discard) event->accept(); else event->ignore();
}
} // namespace nebula
