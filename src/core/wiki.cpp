#include "nebula/core/wiki.hpp"

namespace nebula {
std::optional<WikiLink> parse_wiki_link(const std::string& text)
{
    if (text.size() < 5 || text.substr(0, 2) != "[[" || text.substr(text.size() - 2) != "]]")
        return {};
    WikiLink result;
    std::string anchor;
    auto* field = &result.page;
    bool escaped = false, has_anchor = false;
    for (std::size_t i = 2; i < text.size() - 2; ++i) {
        const char ch = text[i];
        if (ch == '\n' || ch == '\r' || ch == '\0')
            return {};
        if (escaped) {
            if (std::string("\\[]#|").find(ch) == std::string::npos)
                return {};
            *field += ch;
            escaped = false;
        } else if (ch == '\\')
            escaped = true;
        else if (ch == '[' || ch == ']')
            return {};
        else if (ch == '|' && !result.custom_label) {
            result.custom_label = true;
            field = &result.label;
        } else if (ch == '|' && result.custom_label)
            return {};
        else if (ch == '#' && !result.custom_label) {
            if (has_anchor)
                return {};
            has_anchor = true;
            field = &anchor;
        } else
            *field += ch;
    }
    if (escaped)
        return {};
    auto trim = [](std::string& value) {
        const auto start = value.find_first_not_of(" \t");
        value =
            start == std::string::npos ? std::string{} : value.substr(start, value.find_last_not_of(" \t") - start + 1);
    };
    trim(result.page);
    trim(anchor);
    if (result.custom_label && result.label.empty())
        return {};
    if (has_anchor) {
        if (anchor.empty())
            return {};
        if (anchor.front() == '^') {
            result.block = anchor.substr(1);
            if (result.block.empty())
                return {};
        } else
            result.heading = anchor;
    } else if (result.page.empty())
        return {};
    return result;
}
} // namespace nebula
