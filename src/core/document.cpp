#include "nebula/core/document.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace nebula {
namespace {

std::size_t find(const Page& page, const std::string& id)
{
    const auto it = std::find_if(page.blocks.begin(), page.blocks.end(),
                                 [&](const Block& block) { return block.id == id; });
    if (it == page.blocks.end())
        throw std::invalid_argument("Unknown block: " + id);
    return static_cast<std::size_t>(it - page.blocks.begin());
}

std::size_t subtree_end(const Page& page, std::size_t start)
{
    std::unordered_set<std::string> descendants{page.blocks[start].id};
    auto end = start + 1;
    while (end < page.blocks.size() && descendants.count(page.blocks[end].parent_id)) {
        descendants.insert(page.blocks[end].id);
        ++end;
    }
    return end;
}

std::optional<std::size_t> previous_sibling(const Page& page, std::size_t index)
{
    for (auto i = index; i > 0;) {
        --i;
        if (page.blocks[i].id == page.blocks[index].parent_id)
            break;
        if (page.blocks[i].parent_id == page.blocks[index].parent_id)
            return i;
    }
    return std::nullopt;
}

} // namespace

void validate(const Page& page)
{
    if (page.id.empty() || page.blocks.empty())
        throw std::invalid_argument("A page needs an ID and at least one block");
    std::unordered_set<std::string> ids;
    std::vector<std::string> ancestors;
    for (const auto& block : page.blocks) {
        if (int(block.kind)<0 || int(block.kind)>6)
            throw std::invalid_argument("Unknown block kind");
        if (block.id.empty() || !ids.insert(block.id).second)
            throw std::invalid_argument("Block IDs must be nonempty and unique");
        while (!ancestors.empty() && ancestors.back() != block.parent_id)
            ancestors.pop_back();
        if (!block.parent_id.empty() && ancestors.empty())
            throw std::invalid_argument("Parent must be an active ancestor in preorder");
        ancestors.push_back(block.id);
        if (block.kind==BlockKind::Divider && !block.runs.empty())
            throw std::invalid_argument("Divider blocks cannot contain text");
        for (const auto& run : block.runs) {
            if (run.text.find('\n') != std::string::npos || run.text.find('\r') != std::string::npos)
                throw std::invalid_argument("Hard line breaks must be separate blocks");
            if (run.reference && (run.reference->target_id.empty()==run.reference->unresolved_name.empty()))
                throw std::invalid_argument("References need either a target ID or an unresolved name");
            if (run.reference && (int(run.reference->kind)<0 || int(run.reference->kind)>1))
                throw std::invalid_argument("Unknown reference kind");
            if (run.asset && run.asset->label.empty())
                throw std::invalid_argument("Inline asset cards need a label");
        }
        if ((block.kind == BlockKind::Image || block.kind == BlockKind::Attachment)
            && block.asset_label.empty())
            throw std::invalid_argument("Asset cards need a label");
    }
}

int depth(const Page& page, const std::string& block_id)
{
    validate(page);
    auto index = find(page, block_id);
    int result = 0;
    while (!page.blocks[index].parent_id.empty()) {
        index = find(page, page.blocks[index].parent_id);
        ++result;
    }
    return result;
}

bool move_subtree(Page& page, const std::string& block_id, int direction)
{
    validate(page);
    if (direction != -1 && direction != 1)
        throw std::invalid_argument("Move direction must be -1 or 1");
    const auto start = find(page, block_id);
    const auto end = subtree_end(page, start);
    if (direction == -1) {
        const auto previous = previous_sibling(page, start);
        if (!previous)
            return false;
        std::rotate(page.blocks.begin() + *previous, page.blocks.begin() + start,
                    page.blocks.begin() + end);
    } else {
        if (end == page.blocks.size() || page.blocks[end].parent_id != page.blocks[start].parent_id)
            return false;
        const auto next_end = subtree_end(page, end);
        std::rotate(page.blocks.begin() + start, page.blocks.begin() + end,
                    page.blocks.begin() + next_end);
    }
    validate(page);
    return true;
}

bool indent(Page& page, const std::string& block_id)
{
    validate(page);
    const auto index = find(page, block_id);
    const auto previous = previous_sibling(page, index);
    if (!previous)
        return false;
    page.blocks[index].parent_id = page.blocks[*previous].id;
    validate(page);
    return true;
}

bool relocate_subtree(Page& page, const std::string& block_id,
                      const std::string& before_id, const std::string& parent_id)
{
    validate(page);
    const auto start=find(page,block_id), end=subtree_end(page,start);
    for (auto i=start;i<end;++i)
        if (page.blocks[i].id==parent_id || (!before_id.empty() && page.blocks[i].id==before_id))
            throw std::invalid_argument("A block cannot be dropped into its own subtree");
    Page moved=page;
    std::vector<Block> fragment(page.blocks.begin()+start,page.blocks.begin()+end);
    fragment.front().parent_id=parent_id;
    moved.blocks.erase(moved.blocks.begin()+start,moved.blocks.begin()+end);
    std::size_t position=moved.blocks.size();
    if (!before_id.empty()) {
        position=find(moved,before_id);
        if (moved.blocks[position].parent_id!=parent_id)
            throw std::invalid_argument("Drop position must be a sibling of the moved block");
    } else if (!parent_id.empty()) position=subtree_end(moved,find(moved,parent_id));
    moved.blocks.insert(moved.blocks.begin()+position,fragment.begin(),fragment.end());
    validate(moved);
    const bool changed=!std::equal(page.blocks.begin(),page.blocks.end(),moved.blocks.begin(),
        [](const Block& a,const Block& b){return a.id==b.id && a.parent_id==b.parent_id;});
    if (changed) page=std::move(moved);
    return changed;
}

bool outdent(Page& page, const std::string& block_id)
{
    validate(page);
    const auto start = find(page, block_id);
    if (page.blocks[start].parent_id.empty())
        return false;
    const auto parent = find(page, page.blocks[start].parent_id);
    const auto new_parent = page.blocks[parent].parent_id;
    const auto end = subtree_end(page, start);
    const auto parent_end = subtree_end(page, parent);
    page.blocks[start].parent_id = new_parent;
    std::rotate(page.blocks.begin() + start, page.blocks.begin() + end,
                page.blocks.begin() + parent_end);
    validate(page);
    return true;
}

void transfer_subtree(Page& source, Page& destination, const std::string& block_id,
                      const std::string& empty_source_id)
{
    validate(source); validate(destination);
    if (source.id==destination.id) throw std::invalid_argument("Choose a different destination page");
    std::unordered_set<std::string> ids;
    for (const auto& block:source.blocks) ids.insert(block.id);
    for (const auto& block:destination.blocks)
        if (!ids.insert(block.id).second) throw std::invalid_argument("Block IDs must be unique across pages");
    const auto start=find(source,block_id), end=subtree_end(source,start);
    auto next_source=source, next_destination=destination;
    std::vector<Block> fragment(source.blocks.begin()+start,source.blocks.begin()+end);
    fragment.front().parent_id.clear();
    next_destination.blocks.insert(next_destination.blocks.end(),fragment.begin(),fragment.end());
    next_source.blocks.erase(next_source.blocks.begin()+start,next_source.blocks.begin()+end);
    if (next_source.blocks.empty()) {
        if (empty_source_id.empty() || ids.count(empty_source_id))
            throw std::invalid_argument("An emptied source needs a fresh paragraph identity");
        next_source.blocks.push_back({empty_source_id,"",BlockKind::Paragraph,{}});
    }
    validate(next_source); validate(next_destination);
    source=std::move(next_source); destination=std::move(next_destination);
}

Page sample_page()
{
    Page page{"sample-journal", {
        {"heading", "", BlockKind::Heading, {{"A day in Nebula", true}}},
        {"intro", "", BlockKind::Paragraph, {{"Write a paragraph. Press Enter for a new block and Shift+Enter for a line break."}}},
        {"list", "", BlockKind::Bullet, {{"Move blocks with Alt+Up/Down; nest with Tab and Shift+Tab."}}},
        {"child", "list", BlockKind::Bullet, {{"Nested blocks move with their parent."}}},
        {"todo", "", BlockKind::Todo, {{"Try selecting text across several blocks."}}},
        {"reference", "", BlockKind::Paragraph, {{"A stable reference to "},
            {"the heading", false, false, Reference{ReferenceKind::Block, "heading"}},
            {" survives editing its title. Ctrl+click to follow it."}}},
        {"unicode", "", BlockKind::Paragraph, {{u8"日本語のメモ · café · 🌙 · العربية"}}},
        {"image", "", BlockKind::Image, {}, "Sample image representation"},
        {"attachment", "", BlockKind::Attachment, {}, "journal-notes.txt"}
    }};
    validate(page);
    return page;
}

Page long_page(int count)
{
    if (count < 1)
        throw std::invalid_argument("Fixture must have at least one block");
    Page page{"long-page", {}};
    page.blocks.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        page.blocks.push_back({"block-" + std::to_string(i), "", BlockKind::Paragraph,
            {{"Paragraph " + std::to_string(i) + ": A local document keeps ideas connected. "
              "This text wraps as the editor becomes narrower."}}});
    return page;
}

} // namespace nebula
