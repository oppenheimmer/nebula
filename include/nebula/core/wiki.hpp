#pragma once
#include <optional>
#include <string>

namespace nebula {
struct WikiLink {
    std::string page, heading, block, label;
    bool custom_label = false;
};
// [[Page]], [[Page#Heading]], [[Page#^block-id]], optionally |label.
// An empty page selects the current page for heading/block references.
// Backslash escapes only backslash, brackets, # and |. Invalid input stays text.
std::optional<WikiLink> parse_wiki_link(const std::string& text);
} // namespace nebula
