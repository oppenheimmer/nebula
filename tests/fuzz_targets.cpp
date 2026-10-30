// Fuzz targets for every parser that reads untrusted input (SUG-40).
// Without libFuzzer, main() runs a deterministic mutation smoke test; build with
// -DNEBULA_LIBFUZZER=ON (Clang with libFuzzer) for coverage-guided fuzzing.
#include "nebula/attachments/manifest.hpp"
#include "nebula/core/wiki.hpp"
#include "nebula/format/codec.hpp"
#include "nebula/interchange/interchange.hpp"
#include "nebula/storage/workspace.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace nebula;
namespace {
std::filesystem::path path(const QString& value) { return std::filesystem::u8path(value.toUtf8().constData()); }
void write(const QString& file, const std::string& data) { QFile out(file); if (out.open(QIODevice::WriteOnly)) out.write(data.data(), qint64(data.size())); }
// Parsers must reject malformed input with an exception, never crash or hang.
template <typename F> void expect_clean(F&& f) { try { f(); } catch (const std::exception&) {} }

const std::map<std::string, std::function<void(const std::string&)>>& targets()
{
    static const std::map<std::string, std::function<void(const std::string&)>> all{
        {"wiki", [](const std::string& input) { expect_clean([&] { parse_wiki_link(input); }); }},
        {"codec", [](const std::string& input) {
            expect_clean([&] { decode_document(input); });
            expect_clean([&] { Block block; decode_block(block, input); });
            expect_clean([&] { View view; decode_view(view, input); });
            expect_clean([&] { decode_value(input); });
        }},
        {"manifest", [](const std::string& input) {
            QTemporaryDir root; QDir(root.path()).mkpath("attachments");
            write(root.filePath("nebula.sqlite3"), "db"); write(root.filePath("backup-manifest.json"), input);
            expect_clean([&] { files::verify_backup_manifest(path(root.path()), "workspace", true); });
        }},
        {"json", [](const std::string& input) {
            QTemporaryDir root; QDir(root.path()).mkpath("export/attachments");
            write(root.filePath("export/workspace.json"), input);
            expect_clean([&] { interchange::import_json(path(root.filePath("export")), path(root.filePath("imported"))); });
        }},
        {"markdown", [](const std::string& input) {
            QTemporaryDir root; QDir(root.path()).mkpath("input/attachments");
            write(root.filePath("input/attachments/file"), "bytes"); write(root.filePath("input/a.md"), input);
            auto workspace = Workspace::open(path(root.filePath("workspace")));
            const auto database = workspace.create_database("Notes");
            expect_clean([&] { interchange::import_markdown(workspace, path(root.filePath("input")), database.id); });
        }},
    };
    return all;
}
const std::map<std::string, std::vector<std::string>>& seeds()
{
    static const std::map<std::string, std::vector<std::string>> all{
        {"wiki", {"[[Page]]", "[[Page#Heading|label]]", "[[#^block-id]]", "[[A\\|B\\]]]", "[[ é 日本 ]]"}},
        {"codec", {R"({"version":1,"id":"p","blocks":[{"id":"a","parent":"","kind":0,"payload":{"version":3,"runs":[{"text":"x","bold":false,"italic":false,"reference":{"kind":1,"target":"a","unresolved_name":"","scope":"","custom_label":false}}],"asset_label":"","checked":false,"attachment_id":""}}]})",
                   R"({"version":2,"visible":["p"],"filters":[{"property":"p","comparison":1,"value":{"version":1,"type":1,"value":"x"}}],"sort":"","ascending":true,"link_filters":[{"relation":0,"page":"q"}]})",
                   R"({"version":1,"type":5,"value":["a","b"]})"}},
        {"manifest", {R"({"version":1,"workspace_id":"workspace","files":[{"path":"nebula.sqlite3","size":"2","sha256":"0000000000000000000000000000000000000000000000000000000000000000"}]})"}},
        {"json", {R"({"format":"nebula-workspace","version":1,"schema":9,"tables":{"workspace_meta":[],"databases":[],"pages":[],"blocks":[],"property_definitions":[],"property_values":[],"saved_views":[],"deleted_blocks":[],"attachments":[]},"files":[]})"}},
        {"markdown", {"# Title\n\n<a id=\"block-x\"></a>\n## Heading\n\n- [x] done *it*\n\n[link](a.md#block-x) ![](attachments/file) [f](attachments/fi%6Ce)\n---\n"}},
    };
    return all;
}
std::uint64_t state = 0x9e3779b97f4a7c15ULL;
std::uint64_t next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
std::string mutate(std::string input, const std::vector<std::string>& pool)
{
    static const char* tokens[] = {"[[", "]]", "\\", "{", "}", "\"", ":", ",", "#", "^", "|", "%2e%2e", "../", "\n", "\x00", "\xff", "9223372036854775808", "-1", "null", "true", "[", "]"};
    const int steps = 1 + int(next() % 4);
    for (int i = 0; i < steps; ++i) {
        const auto at = input.empty() ? 0 : std::size_t(next() % (input.size() + 1));
        switch (next() % 6) {
        case 0: if (!input.empty() && at < input.size()) input[at] = char(input[at] ^ (1 << (next() % 8))); break;
        case 1: input.insert(at, 1, char(next() % 256)); break;
        case 2: if (at < input.size()) input.erase(at, 1 + std::size_t(next() % 8)); break;
        case 3: { const auto* token = tokens[next() % (sizeof(tokens) / sizeof(*tokens))]; input.insert(at, token, std::max<std::size_t>(1, std::strlen(token))); break; }
        case 4: if (!input.empty()) input.insert(at, input.substr(next() % input.size(), 1 + next() % 16)); break;
        default: { const auto& other = pool[next() % pool.size()]; if (!other.empty()) input.insert(at, other.substr(next() % other.size())); break; }
        }
    }
    return input.substr(0, 1 << 16);
}
} // namespace

#ifdef NEBULA_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    static const std::string target = std::getenv("NEBULA_FUZZ_TARGET") ? std::getenv("NEBULA_FUZZ_TARGET") : "codec";
    targets().at(target)(std::string(reinterpret_cast<const char*>(data), size));
    return 0;
}
#else
// nebula_fuzz [TARGET] [ITERATIONS]: deterministic mutations of built-in seeds.
int main(int argc, char** argv)
{
    const std::string only = argc > 1 ? argv[1] : "all";
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 200;
    for (const auto& entry : targets()) {
        if (only != "all" && only != entry.first) continue;
        const auto& pool = seeds().at(entry.first);
        // Workspace-creating targets are slower; they run a tenth as many cases.
        const int count = entry.first == "json" || entry.first == "markdown" || entry.first == "manifest" ? std::max(1, iterations / 10) : iterations;
        for (const auto& seed : pool) entry.second(seed);
        for (int i = 0; i < count; ++i) entry.second(mutate(pool[next() % pool.size()], pool));
        std::cout << "fuzz " << entry.first << ": " << count + int(pool.size()) << " inputs\n";
    }
    return 0;
}
#endif
