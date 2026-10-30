#pragma once
#include "nebula/storage/workspace.hpp"

namespace nebula {
// Link completion/picker candidates. Rows are read once per window session with
// a single lightweight query; the open page's unsaved blocks overlay them.
class LinkTargets {
public:
    explicit LinkTargets(const Workspace& workspace) : workspace_(workspace) {}
    void invalidate() { loaded_ = false; rows_.clear(); }
    // Candidates with the current page replaced by its pending document/title.
    std::vector<LinkTargetRow> rows(const PageRecord* current = nullptr, const std::string& current_title = {},
                                    const std::string& current_database = {});

private:
    const Workspace& workspace_;
    bool loaded_ = false;
    std::vector<LinkTargetRow> rows_;
};
} // namespace nebula
