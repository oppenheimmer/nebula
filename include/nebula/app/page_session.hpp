#pragma once
#include "nebula/storage/workspace.hpp"

namespace nebula {
// GUI-independent application boundary. The editor owns per-page undo; this
// session owns pending values and persistence, never rewriting history on save.
class PageSession {
public:
    explicit PageSession(Workspace& workspace) : workspace_(workspace) {}
    void open(const std::string& id);
    void close();
    const PageRecord& record() const;
    bool has_page() const { return page_.has_value(); }
    bool dirty() const { return dirty_; }
    const std::string& error() const { return error_; }
    void replace_document(Page page);
    void set_title(std::string title);
    void set_property(const std::string& id, PropertyValue value);
    bool save();
private:
    Workspace& workspace_;
    std::optional<PageRecord> page_;
    bool dirty_ = false;
    std::string error_;
};
} // namespace nebula
