#include "nebula/interchange/interchange.hpp"
#include <chrono>
#include <iostream>

using namespace nebula;
namespace {
void pause_at_boundary()
{
    std::cout << "ready\n" << std::flush;
    std::cin.get();
}
} // namespace

// Public operations only: the parent kills this process at a reported boundary.
int main(int argc, char** argv)
{
    if (argc != 6)
        return 2;
    try {
        const std::string mode = argv[1];
        const auto root = std::filesystem::u8path(argv[2]);
        const auto source = std::filesystem::u8path(argv[4]);
        const auto output = std::filesystem::u8path(argv[5]);
        if (mode == "maintenance") {
            auto workspace = Workspace::read_only(root);
            const auto start = std::chrono::steady_clock::now();
            std::cout << "ready\n" << std::flush;
            int cycles = 0;
            do {
                workspace.backup_to(output / "backup");
                interchange::export_json(workspace, output / "json");
                ++cycles;
                if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(4500))
                    break;
                std::filesystem::remove_all(output / "backup");
                std::filesystem::remove_all(output / "json");
            } while (true);
            std::cout << "cycles=" << cycles << '\n';
            return 0;
        }
        auto workspace = Workspace::open(root);
        auto page = workspace.load_page(argv[3]);
        if (mode.rfind("attachment-", 0) == 0) {
            auto staged = workspace.prepare_attachment(source, false);
            staged.copy([&](auto, auto) {
                if (mode == "attachment-copy")
                    pause_at_boundary();
                return true;
            });
            if (mode == "attachment-promoted")
                pause_at_boundary();
            workspace.commit_attachment(page, page.document.blocks.front().id, staged);
            pause_at_boundary();
        } else {
            auto edit = [&] {
                if (mode.rfind("trash-", 0) == 0)
                    workspace.trash_page(page.document.id, true);
                else {
                    page.document.blocks.front().runs.push_back({" committed edits"});
                    workspace.save_page(page);
                }
            };
            if (mode.find("pending") != std::string::npos)
                workspace.transaction([&] {
                    edit();
                    pause_at_boundary();
                });
            else {
                edit();
                pause_at_boundary();
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
