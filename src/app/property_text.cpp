#include "nebula/app/property_text.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace nebula {
namespace {
std::string trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
} // namespace

std::string format_property(const PropertyValue& value)
{
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) return {};
        else if constexpr (std::is_same_v<T, std::string>) return v;
        else if constexpr (std::is_same_v<T, Date>) return v.iso;
        else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(16) << v; return out.str();
        } else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
        else {
            std::string result;
            for (const auto& tag : v) { if (!result.empty()) result += ", "; result += tag; }
            return result;
        }
    }, value);
}

PropertyValue parse_property(PropertyKind kind, const std::string& text)
{
    const auto value = trim(text);
    if (value.empty())
        return {};
    PropertyValue result;
    switch (kind) {
    case PropertyKind::Text: result = text; break;
    case PropertyKind::Number: {
        auto number = value;
        if (std::count(number.begin(), number.end(), ',') == 1 && number.find('.') == std::string::npos)
            std::replace(number.begin(), number.end(), ',', '.');
        std::istringstream in(number); in.imbue(std::locale::classic());
        double parsed = 0; in >> parsed;
        if (in.fail() || !in.eof() || !std::isfinite(parsed))
            throw std::invalid_argument("Enter a number");
        result = parsed; break;
    }
    case PropertyKind::Date: result = Date{value}; break;
    case PropertyKind::Checkbox: {
        const auto word = lower(value);
        if (word == "true" || word == "1" || word == "yes") result = true;
        else if (word == "false" || word == "0" || word == "no") result = false;
        else throw std::invalid_argument("Enter true or false for a checkbox");
        break;
    }
    case PropertyKind::Tags: {
        std::vector<std::string> tags; std::stringstream in(value); std::string tag;
        while (std::getline(in, tag, ',')) {
            tag = trim(tag);
            if (!tag.empty() && std::find(tags.begin(), tags.end(), tag) == tags.end())
                tags.push_back(tag);
        }
        if (tags.empty()) return {};
        result = std::move(tags); break;
    }
    }
    validate_value(kind, result);
    return result;
}
} // namespace nebula
