#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nebula {

enum class BlockKind { Paragraph, Heading, Bullet, Todo, Image, Attachment, Divider };
enum class ReferenceKind { Page, Block };

struct Reference {
    Reference(ReferenceKind type=ReferenceKind::Page, std::string target={},
              std::string unresolved={}, std::string scope={}, bool custom=true)
        : kind(type),target_id(std::move(target)),unresolved_name(std::move(unresolved)),
          database_scope(std::move(scope)), custom_label(custom) {}
    ReferenceKind kind = ReferenceKind::Page;
    std::string target_id;
    std::string unresolved_name;
    std::string database_scope;
    bool custom_label = true; // Legacy labels remain fixed; new default labels opt in.
};
bool operator==(const Reference& a, const Reference& b);
inline bool operator!=(const Reference& a, const Reference& b) { return !(a==b); }
struct ReferenceLocation { std::string block; std::size_t first_run = 0, run_count = 0; };

struct Asset {
    Asset(bool is_image=false, std::string name={}, std::string attachment={})
        : image(is_image), label(std::move(name)), attachment_id(std::move(attachment)) {}
    bool image = false;
    std::string label;
    std::string attachment_id;
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
          std::vector<Run> content = {}, std::string label = {}, bool is_checked = false, std::string attachment={})
        : id(std::move(identity)), parent_id(std::move(parent)), kind(type),
          runs(std::move(content)), asset_label(std::move(label)), checked(is_checked), attachment_id(std::move(attachment)) {}

    std::string id;
    std::string parent_id; // Empty for a root; vector order is tree preorder.
    BlockKind kind = BlockKind::Paragraph;
    std::vector<Run> runs;
    std::string asset_label;
    bool checked = false;
    std::string attachment_id; // Empty for legacy representation-only cards.
};

struct Page {
    std::string id;
    std::vector<Block> blocks;
};

// Throws std::invalid_argument for an invalid prototype document.
void validate(const Page& page);
// Default references inside a block title use [link], avoiding recursive labels.
std::string reference_title(const Block& block);
int depth(const Page& page, const std::string& block_id);
bool move_subtree(Page& page, const std::string& block_id, int direction);
// Move before a sibling, or append under parent when before_id is empty.
// Reject cycles/invalid destinations without changing the input page.
bool relocate_subtree(Page& page, const std::string& block_id,
                      const std::string& before_id, const std::string& parent_id);
// Append a subtree as a root on another page, preserving every transferred ID.
// If the source becomes empty, insert the supplied fresh paragraph ID.
// Invalid input leaves both pages unchanged.
void transfer_subtree(Page& source, Page& destination, const std::string& block_id,
                      const std::string& empty_source_id);
void insert_attachment(Page& page, const std::string& after, const std::string& attachment,
    const std::string& name, bool image, const std::string& card, const std::string& following);
bool indent(Page& page, const std::string& block_id);
bool outdent(Page& page, const std::string& block_id);
Page sample_page();
Page long_page(int count);

} // namespace nebula
