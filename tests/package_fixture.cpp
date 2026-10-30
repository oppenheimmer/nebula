#include "nebula/attachments/files.hpp"
#include "nebula/storage/workspace.hpp"
#include <fstream>
#include <iostream>

using namespace nebula;
namespace {
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void verify(const std::filesystem::path& root)
{
    auto workspace = Workspace::read_only(root);
    std::string target, entry;
    for (const auto& page : workspace.page_summaries()) {
        if (page.title == "Package target")
            target = page.id;
        if (page.title == "Package entry")
            entry = page.id;
    }
    require(!target.empty() && !entry.empty(), "Package pages are missing");
    require(workspace.search("Package body").size() == 1, "Package search differs");
    require(workspace.backlinks(ReferenceKind::Page, target).size() == 1, "Package backlink differs");
    require(workspace.resolve_reference({ReferenceKind::Block, "package-heading"}).page_id == target,
            "Package block identity differs");
    const auto page = workspace.load_page(entry);
    bool dated = false;
    for (const auto& [id, value] : page.properties) {
        (void)id;
        if (const auto* date = std::get_if<Date>(&value))
            dated = date->iso == "2026-10-29";
    }
    require(dated, "Package diary date differs");
    const auto attachments = workspace.attachments();
    require(attachments.size() == 1, "Package attachment count differs");
    std::ifstream input(workspace.attachment_path(attachments.front().id), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    require(bytes == "retained package attachment", "Package attachment bytes differ");
    require(workspace.audit_attachments().empty(), "Package attachment audit failed");
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    try {
        const auto root = std::filesystem::u8path(argv[2]);
        if (std::string(argv[1]) == "create") {
            auto workspace = Workspace::open(root);
            const auto notes = workspace.create_database("Notes");
            const auto target = workspace.create_page(notes.id, "Package target", {},
                                                      {{"package-heading", {}, BlockKind::Heading, {{u8"見出し"}}}});
            const auto journal = workspace.create_journal();
            auto entry = workspace.create_dated_entry(journal.id, "2026-10-29");
            entry.title = "Package entry";
            entry.document.blocks.front().runs = {
                {"Package body ", true}, {"target", false, false, Reference{ReferenceKind::Block, "package-heading"}}};
            workspace.save_page(entry);
            const auto original = root.parent_path() / "original.bin";
            {
                std::ofstream file(original, std::ios::binary);
                file << "retained package attachment";
                require(bool(file), "Cannot write package source fixture");
            }
            workspace.import_attachment(entry, entry.document.blocks.front().id, original, false);
            std::filesystem::remove(original);
        } else if (std::string(argv[1]) != "verify")
            return 2;
        verify(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
