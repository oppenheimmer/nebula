#include "nebula/core/document.hpp"
#include "nebula/core/database.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>

using namespace nebula;

namespace {
void check(bool condition, const std::string& description)
{
    if (!condition)
        throw std::runtime_error(description);
}

void rejects(const std::function<void()>& action)
{
    try { action(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Expected invalid_argument");
}

Page tree()
{
    return {"page", {{"a", "", BlockKind::Paragraph, {{"A"}}},
                     {"a1", "a", BlockKind::Paragraph, {{"child A"}}},
                     {"a2", "a", BlockKind::Paragraph, {{"second child A"}}},
                     {"b", "", BlockKind::Paragraph, {{"B"}}},
                     {"b1", "b", BlockKind::Paragraph, {{"child B"}}},
                     {"c", "", BlockKind::Paragraph, {{"C"}}}}};
}

std::string ids(const Page& page)
{
    std::string result;
    for (const auto& block : page.blocks)
        result += block.id + ",";
    return result;
}
} // namespace

int main()
{
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"sample and UTF-8 content", [] {
            auto page = sample_page(); validate(page);
            check(page.blocks[6].runs.front().text == u8"日本語のメモ · café · 🌙 · العربية", "UTF-8 changed");
            check(depth(page, "child") == 1, "Incorrect nested depth");
        }},
        {"page and block identity requirements", [] {
            rejects([] { validate({"", {}}); });
            auto page = tree(); page.blocks[1].id = "a";
            rejects([&] { validate(page); });
            page = tree(); page.blocks[1].id.clear();
            rejects([&] { validate(page); });
        }},
        {"missing parents, cycles and invalid preorder", [] {
            auto page = tree(); page.blocks[1].parent_id = "missing";
            rejects([&] { validate(page); });
            page = tree(); page.blocks[0].parent_id = "a1";
            rejects([&] { validate(page); });
            page = tree(); page.blocks[5].parent_id = "a";
            rejects([&] { validate(page); });
        }},
        {"paragraph and resolved reference invariants", [] {
            auto page = tree(); page.blocks[0].runs[0].text = "two\nparagraphs";
            rejects([&] { validate(page); });
            page = tree(); page.blocks[0].runs[0].reference = Reference{ReferenceKind::Page, ""};
            rejects([&] { validate(page); });
            page = tree(); page.blocks[0].kind = BlockKind::Attachment;
            rejects([&] { validate(page); });
            page = tree(); page.blocks[0].runs[0].asset = Asset{true, ""};
            rejects([&] { validate(page); });
        }},
        {"move whole subtrees down and up", [] {
            auto page = tree();
            check(move_subtree(page, "a", 1), "Move failed");
            check(ids(page) == "b,b1,a,a1,a2,c,", "Child order lost");
            check(move_subtree(page, "a", -1), "Reverse move failed");
            check(ids(page) == ids(tree()), "Move failed to restore order");
        }},
        {"sibling move boundaries", [] {
            auto page = tree();
            check(!move_subtree(page, "a", -1), "Moved past beginning");
            check(!move_subtree(page, "c", 1), "Moved past end");
            check(!move_subtree(page, "a1", -1), "Moved outside parent");
            check(!move_subtree(page, "a2", 1), "Moved outside parent");
            rejects([&] { move_subtree(page, "a", 0); });
            rejects([&] { move_subtree(page, "unknown", 1); });
        }},
        {"indent preserves descendants", [] {
            auto page = tree();
            check(indent(page, "b"), "Indent failed");
            check(page.blocks[3].parent_id == "a", "Incorrect new parent");
            check(page.blocks[4].parent_id == "b", "Descendant lost");
            check(depth(page, "b1") == 2, "Incorrect descendant depth");
            check(!indent(page, "a"), "Indented without a preceding sibling");
        }},
        {"outdent moves after former parent's remaining children", [] {
            auto page = tree();
            check(outdent(page, "a1"), "Outdent failed");
            check(ids(page) == "a,a2,a1,b,b1,c,", "Outdented into wrong position");
            check(page.blocks[2].parent_id.empty(), "New parent not root");
            check(!outdent(page, "b"), "Outdented root");
        }},
        {"stable reference survives moves", [] {
            auto page = tree();
            page.blocks.back().runs[0].reference = Reference{ReferenceKind::Block, "a1"};
            move_subtree(page, "a", 1);
            indent(page, "a");
            check(page.blocks.back().runs[0].reference->target_id == "a1", "Reference retargeted");
        }},
        {"long-page fixture", [] {
            const auto page = long_page(1000); validate(page);
            check(page.blocks.size() == 1000, "Wrong fixture size");
            check(page.blocks.back().id == "block-999", "Unstable fixture identity");
            rejects([] { long_page(0); });
        }},
        {"calendar dates remain date-only values", [] {
            check(valid_date("2024-02-29"),"Leap day rejected");
            check(!valid_date("1900-02-29") && valid_date("2000-02-29"),"Gregorian leap rule incorrect");
            check(!valid_date("2026-02-30") && !valid_date("0000-01-01"),"Invalid date accepted");
            validate_value(PropertyKind::Date,Date{"2026-10-29"});
            rejects([]{ validate_value(PropertyKind::Date,std::string("2026-10-29")); });
        }},
        {"properties enforce declared types", [] {
            validate_value(PropertyKind::Number,2.0);
            validate_value(PropertyKind::Checkbox,false);
            rejects([]{ validate_value(PropertyKind::Number,std::string("2")); });
            rejects([]{ validate_value(PropertyKind::Tags,std::vector<std::string>{"a","a"}); });
            rejects([]{ validate_value(PropertyKind(99),{}); });
        }},
        {"views sort numbers and combine typed filters", [] {
            PageRecord a{{"a",{}},"db","Ten",{{"score",10.0},{"done",true}},0,false};
            PageRecord b{{"b",{}},"db","Two",{{"score",2.0},{"done",false}},0,false};
            View view; view.sort_property="score";
            auto sorted=apply_view({a,b},view); check(sorted[0].title=="Two","Numbers sorted as text");
            view.filters={{"score",Comparison::Greater,1.0},{"done",Comparison::Equal,true}};
            auto filtered=apply_view({a,b},view); check(filtered.size()==1 && filtered[0].title=="Ten","Filters did not combine");
        }},
        {"arbitrary subtree relocation preserves descendants", [] {
            auto page=tree();
            check(relocate_subtree(page,"a","b1","b"),"Move into another parent failed");
            check(ids(page)=="b,a,a1,a2,b1,c,","Incorrect insertion position");
            check(depth(page,"a2")==2,"Lost descendant nesting");
            check(relocate_subtree(page,"a","",""),"Move back to root failed");
            check(ids(page)=="b,b1,c,a,a1,a2,","Incorrect root append order");
        }},
        {"invalid drops leave the original tree unchanged", [] {
            auto page=tree(); const auto before=ids(page);
            rejects([&]{relocate_subtree(page,"a","","a1");});
            rejects([&]{relocate_subtree(page,"b","a1","");});
            rejects([&]{relocate_subtree(page,"a","a1","a");});
            rejects([&]{relocate_subtree(page,"c","","missing");});
            check(ids(page)==before,"Rejected drop changed the document");
            check(!relocate_subtree(page,"a","b",""),"No-op move created a change");
        }}
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
