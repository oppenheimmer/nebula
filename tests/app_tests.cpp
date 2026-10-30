// Qt-free application services: navigation history, property text, link targets.
#include "nebula/app/link_targets.hpp"
#include "nebula/app/navigation.hpp"
#include "nebula/app/property_text.hpp"
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

QTEST_GUILESS_MAIN(AppTests)
#include "app_tests.moc"
