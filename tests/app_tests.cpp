// Qt-free application services: navigation history, property text, link targets.
#include "nebula/app/link_targets.hpp"
#include "nebula/app/view_edit.hpp"
#include "nebula/app/interchange.hpp"
#include "nebula/app/navigation.hpp"
#include "nebula/app/property_text.hpp"
#include "nebula/qt/utf8.hpp"
#include <QTemporaryDir>
#include <QTest>
#include <clocale>

using namespace nebula;

class AppTests : public QObject {
    Q_OBJECT
private slots:
    void history_records_moves_and_clears_forward_branches();
    void property_text_is_locale_independent_and_tolerant();
    void link_targets_overlay_the_open_page();
    void utf8_helpers_preserve_unicode_nul_and_validation();
    void view_edit_parses_typed_filters_and_preserves_saved_settings_on_error();
    void interchange_routes_formats_and_keeps_imported_workspace_separate();
};

void AppTests::history_records_moves_and_clears_forward_branches()
{
    NavigationHistory history(2);
    const NavigationHistory::Entry a{"a", {"b1", "b1", 1, 1, 0}}, b{"b", {}}, c{"c", {}};
    QVERIFY(!history.record(a, a)); QVERIFY(!history.can_go(false));
    QVERIFY(history.record(a, b)); QVERIFY(history.record(b, c)); QVERIFY(history.record(c, a));
    QCOMPARE(history.size(false), std::size_t(2)); // Oldest entry dropped at the limit.
    QCOMPARE(history.peek(false)->page, std::string("c"));
    history.commit(false, NavigationHistory::Entry{"a", {}}); QVERIFY(history.can_go(true));
    QCOMPARE(history.peek(true)->page, std::string("a"));
    QVERIFY(history.record(c, b)); QVERIFY(!history.can_go(true));
}
void AppTests::property_text_is_locale_independent_and_tolerant()
{
    std::setlocale(LC_NUMERIC, "de_DE.UTF-8"); // Ignored if unavailable; parsing must not depend on it.
    QCOMPARE(std::get<double>(parse_property(PropertyKind::Number, "4,5")), 4.5);
    QCOMPARE(std::get<double>(parse_property(PropertyKind::Number, " 12.25 ")), 12.25);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parse_property(PropertyKind::Number, "1,000.5"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parse_property(PropertyKind::Number, "12abc"));
    QCOMPARE(std::get<std::vector<std::string>>(parse_property(PropertyKind::Tags, "work, home,, work, ")), (std::vector<std::string>{"work", "home"}));
    QVERIFY(std::holds_alternative<std::monostate>(parse_property(PropertyKind::Tags, " , ")));
    QCOMPARE(std::get<bool>(parse_property(PropertyKind::Checkbox, "Yes")), true);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parse_property(PropertyKind::Date, "2023-02-29"));
    QCOMPARE(format_property(PropertyValue{0.1 + 0.2}), std::string("0.3"));
    QCOMPARE(format_property(PropertyValue{std::vector<std::string>{"a", "b"}}), std::string("a, b"));
    std::setlocale(LC_NUMERIC, "C");
}
void AppTests::link_targets_overlay_the_open_page()
{
    QTemporaryDir folder; auto w = Workspace::open(std::filesystem::u8path(folder.path().toStdString()));
    const auto db = w.create_database("Notes");
    auto page = w.create_page(db.id, "Saved", {}, {{"h", "", BlockKind::Heading, {{"Saved heading"}}}});
    LinkTargets targets(w);
    QCOMPARE(targets.rows().size(), std::size_t(2));
    page.document.blocks.push_back({"new", "", BlockKind::Paragraph, {{"Unsaved"}}});
    const auto rows = targets.rows(&page, "Pending title", "Notes");
    QCOMPARE(rows.size(), std::size_t(3)); QCOMPARE(rows[0].label, std::string("Pending title")); QCOMPARE(rows[2].label, std::string("Unsaved"));
    const auto added = w.create_page(db.id, "Later"); Q_UNUSED(added);
    QCOMPARE(targets.rows().size(), std::size_t(2)); // Cached until invalidated.
    targets.invalidate(); QCOMPARE(targets.rows().size(), std::size_t(4));
}

void AppTests::utf8_helpers_preserve_unicode_nul_and_validation()
{
    const std::string text = std::string("\0", 1) + u8"日本語 é 😀";
    QCOMPARE(utf8::encode(utf8::checked(text)), text);
    QCOMPARE(utf8::encode(utf8::decode(text)), text);
    QVERIFY(utf8::checked({}).isEmpty());
    for (const auto& invalid : {std::string("\xc0\xaf", 2), std::string("\xf0\x9f", 2), std::string("\xff", 1)}) {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, utf8::checked(invalid));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, utf8::checked<std::runtime_error>(invalid));
        QVERIFY(!utf8::decode(invalid).isEmpty());
    }
}

void AppTests::view_edit_parses_typed_filters_and_preserves_saved_settings_on_error()
{
    QTemporaryDir folder; auto w = Workspace::open(std::filesystem::u8path(folder.path().toStdString()));
    const auto db = w.create_database("Notes");
    const auto number = w.add_property(db.id, "Score", PropertyKind::Number);
    const auto date = w.add_property(db.id, "Date", PropertyKind::Date);
    const auto target = w.create_page(db.id, "Target");
    ViewEdit edit; edit.settings.database_id = db.id; edit.settings.name = "Scored";
    edit.settings.visible_properties = {number.id, date.id}; edit.settings.sort_property = number.id;
    edit.settings.ascending = false;
    edit.settings.link_filters = {{LinkRelation::ReferencesPage, target.document.id}};
    edit.filters = {{number.id, Comparison::Greater, "1,5"}, {date.id, Comparison::Empty, "ignored"}};
    const auto saved = save_view_edit(w, edit); QVERIFY(!saved.id.empty());
    QCOMPARE(std::get<double>(saved.filters[0].value), 1.5);
    QVERIFY(std::holds_alternative<std::monostate>(saved.filters[1].value));
    QCOMPARE(saved.visible_properties, edit.settings.visible_properties);
    QCOMPARE(saved.link_filters.front().page_id, target.document.id); QVERIFY(!saved.ascending);
    edit.settings = saved; edit.settings.name = "Do not persist";
    for (const auto& invalid : std::vector<FilterInput>{{number.id, Comparison::Greater, "invalid"},
            {date.id, Comparison::Equal, "2026-02-30"}, {"missing", Comparison::Empty, ""},
            {number.id, Comparison(999), "1"}}) {
        edit.filters = {invalid};
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, save_view_edit(w, edit));
        QCOMPARE(w.views(db.id).size(), std::size_t(1));
        QCOMPARE(w.views(db.id).front().name, saved.name);
    }
    edit.filters = {{"", Comparison::Contains, "title"}, {number.id, Comparison::NotEmpty, "ignored"}};
    const auto updated = save_view_edit(w, edit); QCOMPARE(updated.id, saved.id);
    QCOMPARE(std::get<std::string>(updated.filters[0].value), std::string("title"));
}
void AppTests::interchange_routes_formats_and_keeps_imported_workspace_separate()
{
    QTemporaryDir folder;
    auto path = [&](const char* name) { return std::filesystem::u8path(folder.filePath(name).toStdString()); };
    auto w = Workspace::open(path("workspace")); const auto db = w.create_database("Notes");
    const auto page = w.create_page(db.id, "Original", {}, {{"block", "", BlockKind::Heading, {{u8"日本語"}}}});
    const auto exported = run_interchange(w, InterchangeAction::ExportMarkdown, {}, path("markdown"));
    QCOMPARE(exported.report.pages.size(), std::size_t(1)); QVERIFY(!exported.imported_workspace);
    const auto imported = run_interchange(w, InterchangeAction::ImportMarkdown, path("markdown"), {}, db.id);
    QCOMPARE(imported.report.pages.size(), std::size_t(1));
    QVERIFY(imported.report.pages[0] != page.document.id); QCOMPARE(w.pages(db.id).size(), std::size_t(2));
    run_interchange(w, InterchangeAction::ExportJson, {}, path("json"));
    auto result = run_interchange(w, InterchangeAction::ImportJson, path("json"), path("imported"));
    QVERIFY(result.imported_workspace); QCOMPARE(result.imported_workspace->id(), w.id());
    QCOMPARE(result.imported_workspace->pages(db.id).size(), std::size_t(2));
    result.imported_workspace->rename_database(db.id, "Independent copy");
    QCOMPARE(w.databases().front().name, std::string("Notes"));
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,
        run_interchange(w, InterchangeAction::ImportJson, path("json"), path("workspace")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
        run_interchange(w, InterchangeAction::ImportMarkdown, path("markdown"), {}, "missing"));
    QCOMPARE(w.pages(db.id).size(), std::size_t(2));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, run_interchange(w, InterchangeAction(999), {}, {}));
}

QTEST_GUILESS_MAIN(AppTests)
#include "app_tests.moc"
