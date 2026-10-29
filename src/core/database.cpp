#include "nebula/core/database.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace nebula {
bool valid_date(const std::string& iso)
{
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return false;
    for (std::size_t i = 0; i < iso.size(); ++i)
        if (i != 4 && i != 7 && (iso[i] < '0' || iso[i] > '9')) return false;
    const int y = std::stoi(iso.substr(0, 4)), m = std::stoi(iso.substr(5, 2)), d = std::stoi(iso.substr(8, 2));
    if (y < 1 || m < 1 || m > 12) return false;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d >= 1 && d <= days[m-1] + (m == 2 && (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0)));
}
void validate_value(PropertyKind kind, const PropertyValue& value)
{
    if (int(kind)<0 || int(kind)>4) throw std::invalid_argument("Unknown property type");
    if (std::holds_alternative<std::monostate>(value)) return;
    const auto expected = static_cast<std::size_t>(kind) + 1;
    if (value.index() != expected) throw std::invalid_argument("Property has the wrong type");
    if (const auto* n = std::get_if<double>(&value); n && !std::isfinite(*n))
        throw std::invalid_argument("Numbers must be finite");
    if (const auto* date = std::get_if<Date>(&value); date && !valid_date(date->iso))
        throw std::invalid_argument("Dates must use a valid YYYY-MM-DD calendar date");
    if (const auto* tags = std::get_if<std::vector<std::string>>(&value)) {
        std::unordered_set<std::string> seen;
        for (const auto& tag : *tags)
            if (tag.empty() || !seen.insert(tag).second) throw std::invalid_argument("Tags must be nonempty and unique");
    }
}
int compare_values(const PropertyValue& a, const PropertyValue& b)
{
    if (a.index() != b.index()) return a.index() < b.index() ? -1 : 1;
    return std::visit([&](const auto& left) -> int {
        using T = std::decay_t<decltype(left)>;
        if constexpr (std::is_same_v<T, std::monostate>) return 0;
        else if constexpr (std::is_same_v<T, Date>) {
            const auto& right = std::get<T>(b).iso;
            return left.iso < right ? -1 : left.iso > right ? 1 : 0;
        } else {
            const auto& right = std::get<T>(b);
            return left < right ? -1 : left > right ? 1 : 0;
        }
    }, a);
}
bool matches(const PropertyValue& actual, const Filter& filter)
{
    const bool empty = std::holds_alternative<std::monostate>(actual);
    if (filter.comparison == Comparison::Empty) return empty;
    if (filter.comparison == Comparison::NotEmpty) return !empty;
    if (empty) return false;
    if (filter.comparison == Comparison::Contains) {
        if (const auto* text = std::get_if<std::string>(&actual)) {
            const auto* wanted = std::get_if<std::string>(&filter.value);
            return wanted && text->find(*wanted) != std::string::npos;
        }
        if (const auto* tags = std::get_if<std::vector<std::string>>(&actual)) {
            const auto* wanted = std::get_if<std::string>(&filter.value);
            return wanted && std::find(tags->begin(), tags->end(), *wanted) != tags->end();
        }
        return false;
    }
    if (actual.index() != filter.value.index()) return false;
    const int order = compare_values(actual, filter.value);
    if (filter.comparison == Comparison::Equal) return order == 0;
    if (filter.comparison == Comparison::Less) return order < 0;
    if (filter.comparison == Comparison::Greater) return order > 0;
    return false;
}
std::vector<PageRecord> apply_view(std::vector<PageRecord> pages, const View& view)
{
    const auto value = [](const PageRecord& page, const std::string& id) -> PropertyValue {
        if (id.empty()) return page.title;
        const auto found = page.properties.find(id);
        return found == page.properties.end() ? PropertyValue{} : found->second;
    };
    pages.erase(std::remove_if(pages.begin(), pages.end(), [&](const PageRecord& page) {
        return std::any_of(view.filters.begin(), view.filters.end(), [&](const Filter& filter) {
            return !matches(value(page, filter.property_id), filter);
        });
    }), pages.end());
    std::stable_sort(pages.begin(), pages.end(), [&](const PageRecord& a, const PageRecord& b) {
        const int order = compare_values(value(a, view.sort_property), value(b, view.sort_property));
        if (!order) return a.document.id < b.document.id;
        return view.ascending ? order < 0 : order > 0;
    });
    return pages;
}
} // namespace nebula
