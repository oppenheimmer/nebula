#pragma once
#include <string>

namespace nebula {
// Editor location by stable block identity; independent of the Qt editor.
struct EditorPosition {
    std::string block, anchor_block;
    int offset = 0, anchor_offset = 0, scroll = 0;
};
inline bool operator==(const EditorPosition& a, const EditorPosition& b)
{
    return a.block == b.block && a.offset == b.offset && a.anchor_block == b.anchor_block
        && a.anchor_offset == b.anchor_offset && a.scroll == b.scroll;
}
} // namespace nebula
