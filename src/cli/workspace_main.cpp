#include "nebula/interchange/interchange.hpp"
#include "nebula/storage/workspace.hpp"
#include "nebula/version.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    const std::string command = argc > 1 ? argv[1] : "";
    if (argc == 2 && (command == "--version" || command == "-v")) {
        std::cout << "nebula_workspace " << nebula::version << '\n';
        return 0;
    }
    const bool help = argc == 2 && (command == "--help" || command == "-h");
    if ((argc != 4 || (command != "backup" && command != "restore" && command != "export-md" &&
                       command != "export-json" && command != "import-json")) &&
        (argc != 5 || command != "import-md")) {
        auto& output = help ? std::cout : std::cerr;
        output << "Usage: nebula_workspace backup|restore|export-md|export-json|import-json SOURCE_FOLDER "
                  "NEW_DESTINATION_FOLDER\n"
               << "       nebula_workspace import-md MARKDOWN_FOLDER WORKSPACE_FOLDER DATABASE_ID\n"
               << "       nebula_workspace --help|--version\n";
        return help ? 0 : 2;
    }
    try {
        const auto source = std::filesystem::u8path(argv[2]);
        const auto destination = std::filesystem::u8path(argv[3]);
        nebula::InterchangeReport report;
        if (command == "backup")
            nebula::Workspace::read_only(source).backup_to(destination);
        else if (command == "restore") {
            const auto restored = nebula::Workspace::restore(source, destination);
        } else if (command == "export-md")
            report = nebula::interchange::export_markdown(nebula::Workspace::read_only(source), destination);
        else if (command == "export-json")
            nebula::interchange::export_json(nebula::Workspace::read_only(source), destination);
        else if (command == "import-json") {
            const auto imported = nebula::interchange::import_json(source, destination);
        } else {
            auto workspace = nebula::Workspace::open(destination);
            report = nebula::interchange::import_markdown(workspace, source, argv[4]);
        }
        for (const auto& warning : report.warnings)
            std::cout << "Conversion: " << warning << '\n';
        std::cout << "Completed " << argv[1] << " to " << destination.u8string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
