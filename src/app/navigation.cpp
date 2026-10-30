#include "nebula/app/navigation.hpp"

namespace nebula {

bool NavigationHistory::record(const Entry& previous, const Entry& current)
{
    if (previous.page == current.page && previous.editor == current.editor)
        return false;
    back_.push_back(previous);
    if (back_.size() > limit_)
        back_.erase(back_.begin());
    forward_.clear();
    return true;
}

std::optional<NavigationHistory::Entry> NavigationHistory::peek(bool forward) const
{
    const auto& list = forward ? forward_ : back_;
    if (list.empty())
        return std::nullopt;
    return list.back();
}

void NavigationHistory::commit(bool forward, const std::optional<Entry>& departing)
{
    auto& from = forward ? forward_ : back_;
    auto& to = forward ? back_ : forward_;
    if (from.empty())
        return;
    from.pop_back();
    if (departing)
        to.push_back(*departing);
}

} // namespace nebula
