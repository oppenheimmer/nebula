#pragma once
#include "nebula/storage/workspace.hpp"

namespace nebula {
struct FilterInput {
    std::string property_id;
    Comparison comparison = Comparison::Equal;
    std::string text;
};
struct ViewEdit {
    View settings;
    std::vector<FilterInput> filters;
};
// Parse and validate every field before changing stored view settings.
View save_view_edit(Workspace& workspace, const ViewEdit& edit);
} // namespace nebula
