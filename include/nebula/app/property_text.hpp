#pragma once
#include "nebula/core/database.hpp"

namespace nebula {
// Table cell text for typed values and the reverse conversion. Numbers accept
// either a decimal point or a single decimal comma, independent of the process
// locale. Tags split on commas, ignore empty entries and drop repeated tags.
std::string format_property(const PropertyValue& value);
PropertyValue parse_property(PropertyKind kind, const std::string& text);
} // namespace nebula
