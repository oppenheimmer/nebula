#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include "nebula/core/position.hpp"

namespace nebula {

// Window-lifetime Back/Forward history of page positions.
class NavigationHistory {
public:
    struct Entry { std::string page; EditorPosition editor; };
    explicit NavigationHistory(std::size_t limit = 200) : limit_(limit) {}
    // Records the departing position unless nothing changed. Clears Forward.
    bool record(const Entry& previous, const Entry& current);
    bool can_go(bool forward) const { return !(forward ? forward_ : back_).empty(); }
    std::optional<Entry> peek(bool forward) const;
    // After successfully opening peek(forward): move it across, saving where we were.
    void commit(bool forward, const std::optional<Entry>& departing);
    std::size_t size(bool forward) const { return (forward ? forward_ : back_).size(); }

private:
    std::vector<Entry> back_, forward_;
    std::size_t limit_;
};

} // namespace nebula
