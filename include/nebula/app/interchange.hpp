#pragma once
#include "nebula/storage/workspace.hpp"

namespace nebula {
enum class InterchangeAction { ImportMarkdown, ExportMarkdown, ImportJson, ExportJson };
struct InterchangeResult {
    InterchangeReport report;
    std::unique_ptr<Workspace> imported_workspace;
};
// UI adapters select paths and present reports; this service routes operations
// and owns the imported workspace until the caller gives it to a new window.
InterchangeResult run_interchange(Workspace& workspace, InterchangeAction action,
    const std::filesystem::path& source, const std::filesystem::path& destination,
    const std::string& database = {});
} // namespace nebula
