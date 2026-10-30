#include "nebula/storage/workspace.hpp"
#include <iostream>

int main(int argc,char** argv)
{
    const std::string command=argc>1?argv[1]:"";
    if ((argc!=4 || (command!="backup" && command!="restore" && command!="export-md"))
        && (argc!=5 || command!="import-md")) {
        std::cerr<<"Usage: nebula_workspace backup|restore|export-md SOURCE_FOLDER NEW_DESTINATION_FOLDER\n"
            <<"       nebula_workspace import-md MARKDOWN_FOLDER WORKSPACE_FOLDER DATABASE_ID\n"; return 2;
    }
    try {
        const auto source=std::filesystem::u8path(argv[2]);
        const auto destination=std::filesystem::u8path(argv[3]);
        nebula::InterchangeReport report;
        if (command=="backup") nebula::Workspace::read_only(source).backup_to(destination);
        else if (command=="restore") { const auto restored=nebula::Workspace::restore(source,destination); }
        else if (command=="export-md") report=nebula::Workspace::read_only(source).export_markdown(destination);
        else report=nebula::Workspace::open(destination).import_markdown(source,argv[4]);
        for (const auto& warning:report.warnings) std::cout<<"Conversion: "<<warning<<'\n';
        std::cout<<"Completed "<<argv[1]<<" to "<<destination.u8string()<<'\n'; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
