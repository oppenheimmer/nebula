#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nebula {

enum class BlockKind { Paragraph, Heading, Bullet, Todo, Image, Attachment };
enum class ReferenceKind { Page, Block };

struct Reference {
    Reference(ReferenceKind type=ReferenceKind::Page, std::string target={},
              std::string unresolved={}, std::string scope={})
        : kind(type),target_id(std::move(target)),unresolved_name(std::move(unresolved)),
          database_scope(std::move(scope)) {}
    ReferenceKind kind = ReferenceKind::Page;
    std::string target_id;
    std::string unresolved_name;
    std::string database_scope;
};

struct Asset {
    bool image = false;
    std::string label;
};

struct Run {
    Run(std::string value = {}, bool strong = false, bool emphasis = false,
        std::optional<Reference> target = std::nullopt, std::optional<Asset> card = std::nullopt)
        : text(std::move(value)), bold(strong), italic(emphasis),
          reference(std::move(target)), asset(std::move(card)) {}

    std::string text; // UTF-8; paragraph boundaries are separate blocks.
    bool bold = false;
    bool italic = false;
    std::optional<Reference> reference;
    std::optional<Asset> asset;
};

struct Block {
    Block(std::string identity = {}, std::string parent = {}, BlockKind type = BlockKind::Paragraph,
          std::vector<Run> content = {}, std::string label = {}, bool is_checked = false)
        : id(std::move(identity)), parent_id(std::move(parent)), kind(type),
          runs(std::move(content)), asset_label(std::move(label)), checked(is_checked) {}

    std::string id;
    std::string parent_id; // Empty for a root; vector order is tree preorder.
    BlockKind kind = BlockKind::Paragraph;
    std::vector<Run> runs;
    std::string asset_label; // Prototype card only, not a managed file path.
    bool checked = false;
};

struct Page {
    std::string id;
    std::vector<Block> blocks;
};

// Throws std::invalid_argument for an invalid prototype document.
void validate(const Page& page);
int depth(const Page& page, const std::string& block_id);
bool move_subtree(Page& page, const std::string& block_id, int direction);
// Move before a sibling, or append under parent when before_id is empty.
// Reject cycles/invalid destinations without changing the input page.
bool relocate_subtree(Page& page, const std::string& block_id,
                      const std::string& before_id, const std::string& parent_id);
bool indent(Page& page, const std::string& block_id);
bool outdent(Page& page, const std::string& block_id);
Page sample_page();
Page long_page(int count);

} // namespace nebula
