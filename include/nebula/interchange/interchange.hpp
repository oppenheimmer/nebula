#pragma once
// Markdown and lossless JSON conversion. These adapters use the storage
// module's narrow internal API; they are not Workspace members.
#include "nebula/storage/workspace.hpp"

namespace nebula::interchange {
InterchangeReport export_markdown(const Workspace& workspace, const std::filesystem::path& destination);
InterchangeReport import_markdown(Workspace& workspace, const std::filesystem::path& source,
                                  const std::string& database);
void export_json(const Workspace& workspace, const std::filesystem::path& destination);
Workspace import_json(const std::filesystem::path& source, const std::filesystem::path& destination);
} // namespace nebula::interchange
