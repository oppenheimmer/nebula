#pragma once
#include "nebula/storage/workspace.hpp"
#include <unordered_set>

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
    // Category of the last failed save, for recovery choices such as reload.
    ErrorKind error_kind() const { return error_kind_; }
    bool conflicted() const { return !error_.empty() && error_kind_ == ErrorKind::Conflict; }
    void replace_document(Page page);
    void set_title(std::string title);
    void set_property(const std::string& id, PropertyValue value);
    // Blocks merged into neighbours; they are not archived as deletions on save.
    void note_absorbed(const std::unordered_set<std::string>& blocks);
    bool save();
    // Drops pending values after the user kept them elsewhere or chose to discard them.
    void discard();
    void adopt_saved(PageRecord record); // A committed operation on this page; keeps editor undo.
private:
    Workspace& workspace_;
    std::optional<PageRecord> page_;
    bool dirty_ = false;
    std::string error_;
    ErrorKind error_kind_ = ErrorKind::Io;
    std::unordered_set<std::string> absorbed_;
};
} // namespace nebula
