#include "nebula/app/interchange.hpp"
#include "nebula/interchange/interchange.hpp"

namespace nebula {
InterchangeResult run_interchange(Workspace& workspace, InterchangeAction action,
    const std::filesystem::path& source, const std::filesystem::path& destination, const std::string& database)
{
    switch (action) {
    case InterchangeAction::ImportMarkdown:
        return {interchange::import_markdown(workspace, source, database), {}};
    case InterchangeAction::ExportMarkdown:
        return {interchange::export_markdown(workspace, destination), {}};
    case InterchangeAction::ImportJson:
        return {{}, std::make_unique<Workspace>(interchange::import_json(source, destination))};
    case InterchangeAction::ExportJson:
        interchange::export_json(workspace, destination);
        return {};
    }
    throw std::invalid_argument("Unsupported interchange operation");
}
} // namespace nebula
