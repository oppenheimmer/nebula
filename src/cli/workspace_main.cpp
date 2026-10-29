#include "nebula/storage/workspace.hpp"
#include <iostream>

int main(int argc,char** argv)
{
    if (argc!=4 || (std::string(argv[1])!="backup" && std::string(argv[1])!="restore")) {
        std::cerr<<"Usage: nebula_workspace backup|restore SOURCE_FOLDER NEW_DESTINATION_FOLDER\n"; return 2;
    }
    try {
        const auto source=std::filesystem::u8path(argv[2]);
        const auto destination=std::filesystem::u8path(argv[3]);
        if (std::string(argv[1])=="backup") nebula::Workspace::read_only(source).backup_to(destination);
        else { const auto restored=nebula::Workspace::restore(source,destination); }
        std::cout<<"Completed "<<argv[1]<<" to "<<destination.u8string()<<'\n'; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
