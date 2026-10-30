#include "nebula/app/page_session.hpp"
#include <algorithm>
#include <stdexcept>

namespace nebula {
const PageRecord& PageSession::record() const {
    if (!page_) throw std::logic_error("No page is open");
    return *page_;
}
void PageSession::open(const std::string& id) {
    if (dirty_ && !save()) throw StorageError(error_kind_,error_);
    auto next = workspace_.load_page(id);
    if (next.trashed) throw std::runtime_error("This page is in trash; restore it first");
    page_ = std::move(next); dirty_ = false; error_.clear(); absorbed_.clear();
}
void PageSession::close() {
    if (dirty_ && !save()) throw StorageError(error_kind_,error_);
    page_.reset(); error_.clear(); absorbed_.clear();
}
void PageSession::replace_document(Page page) {
    if (page.id != record().document.id) throw std::invalid_argument("Page identity cannot change during editing");
    validate(page); page_->document = std::move(page); dirty_ = true;
}
void PageSession::set_title(std::string title) { record(); page_->title=std::move(title); dirty_=true; }
void PageSession::set_property(const std::string& id, PropertyValue value) {
    const auto defs=workspace_.properties(record().database_id);
    const auto found=std::find_if(defs.begin(),defs.end(),[&](const auto& d) { return d.id==id; });
    if (found==defs.end()) throw std::invalid_argument("Unknown property");
    validate_value(found->kind,value); page_->properties[id]=std::move(value); dirty_=true;
}
void PageSession::note_absorbed(const std::unordered_set<std::string>& blocks) {
    absorbed_.insert(blocks.begin(),blocks.end());
}
bool PageSession::save() {
    if (!dirty_) return true;
    try { workspace_.save_page(*page_,absorbed_); dirty_=false; error_.clear(); absorbed_.clear(); return true; }
    catch (const std::exception& error) { error_=error.what(); error_kind_=nebula::error_kind(error); return false; }
}
void PageSession::discard() {
    dirty_=false; error_.clear(); absorbed_.clear();
}
void PageSession::adopt_saved(PageRecord record) {
    if (dirty_ || record.document.id!=this->record().document.id)
        throw std::logic_error("Flush the current page before adopting a saved operation");
    page_=std::move(record); error_.clear();
}
} // namespace nebula
