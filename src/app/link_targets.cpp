#include "nebula/app/link_targets.hpp"

namespace nebula {
std::vector<LinkTargetRow> LinkTargets::rows(const PageRecord* current, const std::string& current_title,
                                             const std::string& current_database)
{
    if (!loaded_) {
        rows_ = workspace_.link_targets();
        loaded_ = true;
    }
    if (!current)
        return rows_;
    std::vector<LinkTargetRow> result;
    result.reserve(rows_.size());
    auto overlay = [&](const std::string& database) {
        result.push_back({ReferenceKind::Page, current->document.id, current_title, current->document.id, current_title,
                          database, false});
        for (const auto& block : current->document.blocks) {
            std::string text;
            for (const auto& run : block.runs)
                text += run.text;
            if (text.empty())
                text = block.asset_label.empty() ? "Empty block" : block.asset_label;
            result.push_back({ReferenceKind::Block, block.id, text, current->document.id, current_title, database,
                              block.kind == BlockKind::Heading});
        }
    };
    bool replaced = false;
    for (const auto& row : rows_) {
        if (row.page_id != current->document.id) {
            result.push_back(row);
            continue;
        }
        if (!replaced)
            overlay(row.database_name);
        replaced = true;
    }
    if (!replaced)
        overlay(current_database);
    return result;
}
} // namespace nebula
