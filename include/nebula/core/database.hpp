#pragma once
#include "nebula/core/document.hpp"
#include <cstdint>
#include <map>
#include <variant>

namespace nebula {

enum class PropertyKind { Text, Number, Date, Checkbox, Tags };
struct Date { std::string iso; };
using PropertyValue = std::variant<std::monostate, std::string, double, Date, bool, std::vector<std::string>>;
struct PropertyDefinition { std::string id, database_id, name; PropertyKind kind = PropertyKind::Text; };
struct Database { std::string id, name; bool trashed = false; };
struct PageRecord {
    Page document;
    std::string database_id, title;
    std::map<std::string, PropertyValue> properties;
    std::int64_t revision = 0;
    bool trashed = false;
};
enum class Comparison { Equal, Contains, Less, Greater, Empty, NotEmpty };
struct Filter { std::string property_id; Comparison comparison = Comparison::Equal; PropertyValue value; };
enum class LinkRelation { ReferencesPage, ReferencedByPage };
struct LinkFilter { LinkRelation relation=LinkRelation::ReferencesPage; std::string page_id; };
struct View {
    std::string id, database_id, name;
    std::vector<std::string> visible_properties;
    std::vector<Filter> filters;
    std::string sort_property; // Empty means title.
    bool ascending = true;
    std::vector<LinkFilter> link_filters;
};

bool valid_date(const std::string& iso);
void validate_link_filters(const std::vector<LinkFilter>& filters);
void validate_value(PropertyKind kind, const PropertyValue& value);
bool matches(const PropertyValue& actual, const Filter& filter);
int compare_values(const PropertyValue& a, const PropertyValue& b);
std::vector<PageRecord> apply_view(std::vector<PageRecord> pages, const View& view);

} // namespace nebula
