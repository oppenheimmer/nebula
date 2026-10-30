#include "nebula/core/database.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace nebula {
bool valid_date(const std::string& iso)
{
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-')
        return false;
    for (std::size_t i = 0; i < iso.size(); ++i)
        if (i != 4 && i != 7 && (iso[i] < '0' || iso[i] > '9'))
            return false;
    const int y = std::stoi(iso.substr(0, 4)), m = std::stoi(iso.substr(5, 2)), d = std::stoi(iso.substr(8, 2));
    if (y < 1 || m < 1 || m > 12)
        return false;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d >= 1 && d <= days[m - 1] + (m == 2 && (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0)));
}
void validate_value(PropertyKind kind, const PropertyValue& value)
{
    if (int(kind) < 0 || int(kind) > 4)
        throw std::invalid_argument("Unknown property type");
    if (std::holds_alternative<std::monostate>(value))
        return;
    const auto expected = static_cast<std::size_t>(kind) + 1;
    if (value.index() != expected)
        throw std::invalid_argument("Property has the wrong type");
    if (const auto* n = std::get_if<double>(&value); n && !std::isfinite(*n))
        throw std::invalid_argument("Numbers must be finite");
    if (const auto* date = std::get_if<Date>(&value); date && !valid_date(date->iso))
        throw std::invalid_argument("Dates must use a valid YYYY-MM-DD calendar date");
    if (const auto* tags = std::get_if<std::vector<std::string>>(&value)) {
        std::unordered_set<std::string> seen;
        for (const auto& tag : *tags)
            if (tag.empty() || !seen.insert(tag).second)
                throw std::invalid_argument("Tags must be nonempty and unique");
    }
}
int compare_values(const PropertyValue& a, const PropertyValue& b, const TextRules& rules)
{
    if (a.index() != b.index())
        return a.index() < b.index() ? -1 : 1;
    return std::visit(
        [&](const auto& left) -> int {
            using T = std::decay_t<decltype(left)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return 0;
            else if constexpr (std::is_same_v<T, std::string>) {
                const auto& right = std::get<T>(b);
                if (rules.compare) {
                    const int order = rules.compare(left, right);
                    if (order)
                        return order < 0 ? -1 : 1;
                }
                return left < right ? -1 : left > right ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Date>) {
                const auto& right = std::get<T>(b).iso;
                return left.iso < right ? -1 : left.iso > right ? 1 : 0;
            } else {
                const auto& right = std::get<T>(b);
                return left < right ? -1 : left > right ? 1 : 0;
            }
        },
        a);
}
bool matches(const PropertyValue& actual, const Filter& filter, const TextRules& rules)
{
    const bool empty = std::holds_alternative<std::monostate>(actual);
    if (filter.comparison == Comparison::Empty)
        return empty;
    if (filter.comparison == Comparison::NotEmpty)
        return !empty;
    if (empty)
        return false;
    if (filter.comparison == Comparison::Contains) {
        if (const auto* text = std::get_if<std::string>(&actual)) {
            const auto* wanted = std::get_if<std::string>(&filter.value);
            if (!wanted)
                return false;
            return rules.contains ? rules.contains(*text, *wanted) : text->find(*wanted) != std::string::npos;
        }
        if (const auto* tags = std::get_if<std::vector<std::string>>(&actual)) {
            const auto* wanted = std::get_if<std::string>(&filter.value);
            return wanted && std::find(tags->begin(), tags->end(), *wanted) != tags->end();
        }
        return false;
    }
    if (actual.index() != filter.value.index())
        return false;
    if (filter.comparison == Comparison::Equal) {
        if (const auto* tags = std::get_if<std::vector<std::string>>(&actual)) {
            auto left = *tags, right = std::get<std::vector<std::string>>(filter.value);
            std::sort(left.begin(), left.end());
            std::sort(right.begin(), right.end());
            return left == right;
        }
    }
    const int order = compare_values(actual, filter.value);
    if (filter.comparison == Comparison::Equal)
        return order == 0;
    if (filter.comparison == Comparison::Less)
        return order < 0;
    if (filter.comparison == Comparison::Greater)
        return order > 0;
    return false;
}
std::vector<PageRecord> apply_view(std::vector<PageRecord> pages, const View& view, const TextRules& rules)
{
    if (!view.link_filters.empty())
        throw std::invalid_argument("Reference filters require a workspace query");
    const auto value = [](const PageRecord& page, const std::string& id) -> PropertyValue {
        if (id.empty())
            return page.title;
        const auto found = page.properties.find(id);
        return found == page.properties.end() ? PropertyValue{} : found->second;
    };
    pages.erase(std::remove_if(pages.begin(), pages.end(),
                               [&](const PageRecord& page) {
                                   return std::any_of(
                                       view.filters.begin(), view.filters.end(), [&](const Filter& filter) {
                                           return !matches(value(page, filter.property_id), filter, rules);
                                       });
                               }),
                pages.end());
    std::stable_sort(pages.begin(), pages.end(), [&](const PageRecord& a, const PageRecord& b) {
        const int order = compare_values(value(a, view.sort_property), value(b, view.sort_property), rules);
        if (!order)
            return a.document.id < b.document.id;
        return view.ascending ? order < 0 : order > 0;
    });
    return pages;
}
void validate_view(const View& view, const std::vector<PropertyDefinition>& definitions)
{
    validate_link_filters(view.link_filters);
    if (view.name.empty() || view.name.find('\0') != std::string::npos)
        throw std::invalid_argument("A nonempty view name is required");
    const auto known = [&](const std::string& id) {
        return id.empty() ||
               std::any_of(definitions.begin(), definitions.end(), [&](const auto& d) { return d.id == id; });
    };
    if (!known(view.sort_property))
        throw std::invalid_argument("Unknown sort property");
    std::unordered_set<std::string> visible;
    for (const auto& id : view.visible_properties)
        if (id.empty() || !known(id) || !visible.insert(id).second)
            throw std::invalid_argument("Invalid visible columns");
    for (const auto& filter : view.filters) {
        if (!known(filter.property_id) || int(filter.comparison) < 0 || int(filter.comparison) > 5)
            throw std::invalid_argument("Invalid filter");
        auto kind = PropertyKind::Text;
        for (const auto& definition : definitions)
            if (definition.id == filter.property_id)
                kind = definition.kind;
        if (filter.comparison == Comparison::Contains) {
            if ((kind != PropertyKind::Text && kind != PropertyKind::Tags) ||
                !std::holds_alternative<std::string>(filter.value))
                throw std::invalid_argument("Contains filters require text or tags and a text value");
        } else if (filter.comparison != Comparison::Empty && filter.comparison != Comparison::NotEmpty) {
            validate_value(kind, filter.value);
        }
    }
}
void validate_link_filters(const std::vector<LinkFilter>& filters)
{
    if (filters.size() > 8)
        throw std::invalid_argument("Use at most eight reference filters");
    for (const auto& filter : filters)
        if (int(filter.relation) < 0 || int(filter.relation) > 1 || !valid_identity(filter.page_id))
            throw std::invalid_argument("Select a stable page identity and reference relation");
}
} // namespace nebula
