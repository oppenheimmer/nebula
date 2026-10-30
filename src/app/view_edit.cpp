#include "nebula/app/view_edit.hpp"
#include "nebula/app/property_text.hpp"
#include <algorithm>

namespace nebula {
View save_view_edit(Workspace& workspace, const ViewEdit& edit)
{
    auto view = edit.settings;
    view.filters.clear();
    const auto definitions = workspace.properties(view.database_id);
    for (const auto& input : edit.filters) {
        auto kind = PropertyKind::Text;
        if (!input.property_id.empty()) {
            const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& definition) {
                return definition.id == input.property_id;
            });
            if (found == definitions.end()) throw std::invalid_argument("Filter property is unavailable");
            kind = found->kind;
        }
        auto value = input.comparison == Comparison::Empty || input.comparison == Comparison::NotEmpty
            ? PropertyValue{} : parse_property(input.comparison == Comparison::Contains ? PropertyKind::Text : kind, input.text);
        view.filters.push_back({input.property_id, input.comparison, std::move(value)});
    }
    validate_view(view, definitions);
    workspace.save_view(view);
    return view;
}
} // namespace nebula
