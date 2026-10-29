#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdio>

using namespace nebula;

namespace {

int benchmark(const QString& mode, int count)
{
    const auto page = long_page(count);
    PageEditor single;
    BlockWidgets separate;
    QWidget* widget = mode == "single" ? static_cast<QWidget*>(&single) : &separate;
    widget->resize(900, 700);
    QElapsedTimer timer;
    timer.start();
    if (mode == "single")
        single.set_page(page);
    else
        separate.set_page(page);
    widget->show();
    QApplication::processEvents();
    const auto load_ms = timer.elapsed();
    QTextEdit* input = mode == "single" ? &single : separate.editors().front();
    input->setFocus();
    std::vector<qint64> latencies;
    for (int i = 0; i < 30; ++i) {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
        timer.restart();
        QApplication::sendEvent(input, &key);
        QApplication::processEvents();
        latencies.push_back(timer.nsecsElapsed());
    }
    std::sort(latencies.begin(), latencies.end());
    QJsonObject result{{"mode", mode}, {"blocks", count}, {"qt", qVersion()},
        {"load_ms", load_ms}, {"typing_p95_ms", latencies[28] / 1000000.0},
        {"native_editors", mode == "single" ? 1 : count}};
    if (mode == "single") {
        single.go_to_block("block-" + QString::number(std::min(500, count - 1)));
        timer.restart();
        single.move_current(1);
        QApplication::processEvents();
        result["move_ms"] = timer.elapsed();
    }
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly)) {
        for (const auto& line : status.readAll().split('\n')) {
            if (line.startsWith("VmHWM:"))
                result["peak_rss_kib"] = QString::fromUtf8(line).simplified().split(' ').value(1).toLongLong();
        }
    }
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Nebula editor prototype");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"smoke-test", "Create both editor approaches and exit after event processing."});
    parser.addOption({"benchmark", "Measure single or separate block editors.", "mode"});
    parser.addOption({"blocks", "Number of benchmark blocks (1–10000).", "count", "1000"});
    parser.addOption({"screenshot", "Save a prototype screenshot and exit.", "path"});
    parser.process(app);
    if (parser.isSet("benchmark")) {
        const auto mode = parser.value("benchmark");
        bool valid = false;
        const int count = parser.value("blocks").toInt(&valid);
        if ((mode != "single" && mode != "separate") || !valid || count < 1 || count > 10000) {
            std::fputs("Use --benchmark single|separate --blocks 1..10000\n", stderr);
            return 2;
        }
        return benchmark(mode, count);
    }

    QMainWindow window;
    window.setWindowTitle("Nebula — editor feasibility prototype");
    window.resize(1050, 820);
    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    auto* notice = new QLabel("PROTOTYPE  ·  In memory only; edits are discarded when this window closes.", central);
    notice->setStyleSheet("QLabel { padding: 12px; background: #fff3d5; color: #654c13; }");
    layout->addWidget(notice);
    auto* tabs = new QTabWidget(central);
    auto* editor = new PageEditor(tabs);
    auto* comparison = new BlockWidgets(tabs);
    editor->set_page(sample_page());
    comparison->set_page(sample_page());
    tabs->addTab(editor, "Page-wide document");
    tabs->addTab(comparison, "Separate blocks — comparison");
    layout->addWidget(tabs);
    window.setCentralWidget(central);

    auto* toolbar = window.addToolBar("Editing");
    toolbar->setMovable(false);
    auto action = [&](const QString& label, auto callback) {
        auto* item = toolbar->addAction(label);
        editor->addAction(item);
        item->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        QObject::connect(item, &QAction::triggered, editor, callback);
        return item;
    };
    action("Undo", [=] { editor->undo(); })->setShortcut(QKeySequence::Undo);
    action("Redo", [=] { editor->redo(); })->setShortcut(QKeySequence::Redo);
    toolbar->addSeparator();
    action("Bold", [=] { editor->toggle_bold(); })->setShortcut(QKeySequence::Bold);
    action("Italic", [=] { editor->toggle_italic(); })->setShortcut(QKeySequence::Italic);
    action("Heading", [=] { editor->set_block_kind(BlockKind::Heading); });
    action("Paragraph", [=] { editor->set_block_kind(BlockKind::Paragraph); });
    action("Bullet", [=] { editor->set_block_kind(BlockKind::Bullet); });
    action("To-do", [=] { editor->set_block_kind(BlockKind::Todo); });
    action("Check", [=] { editor->toggle_checked(); });
    toolbar->addSeparator();
    action("↑", [=] { editor->move_current(-1); });
    action("↓", [=] { editor->move_current(1); });
    action("Indent", [=] { editor->indent_current(); });
    action("Outdent", [=] { editor->outdent_current(); });
    window.addToolBarBreak();
    auto* insert = window.addToolBar("Insert and fixtures");
    insert->setMovable(false);
    QObject::connect(insert->addAction("Link to heading"), &QAction::triggered, editor,
        [=] { editor->insert_reference("the heading", ReferenceKind::Block, "heading"); });
    QObject::connect(insert->addAction("Image card"), &QAction::triggered, editor,
        [=] { editor->insert_card(true, "Sample image representation"); });
    QObject::connect(insert->addAction("Attachment card"), &QAction::triggered, editor,
        [=] { editor->insert_card(false, "example.txt"); });
    QObject::connect(insert->addAction("Reset sample"), &QAction::triggered, editor,
        [=] { editor->set_page(sample_page()); comparison->set_page(sample_page()); });
    QObject::connect(insert->addAction("Load 1000 blocks"), &QAction::triggered, editor,
        [=] { editor->set_page(long_page(1000)); comparison->set_page(long_page(1000)); });
    QObject::connect(tabs, &QTabWidget::currentChanged, toolbar, [=](int index) {
        toolbar->setEnabled(index == 0);
        insert->setEnabled(index == 0);
    });
    QObject::connect(editor, &PageEditor::reference_activated, &window, [=, &window](int kind, const QString& id) {
        if (kind == static_cast<int>(ReferenceKind::Block) && editor->go_to_block(id))
            window.statusBar()->showMessage("Followed block reference: " + id, 4000);
        else
            window.statusBar()->showMessage("Target not present in this prototype page: " + id, 4000);
    });
    window.statusBar()->showMessage("Enter: new block · Shift+Enter: line break · Alt+↑/↓: move · Tab/Shift+Tab: nest · Ctrl+click: reference");
    window.show();
    if (parser.isSet("smoke-test"))
        QTimer::singleShot(50, &app, &QApplication::quit);
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(100, &app, [&] {
            app.exit(window.grab().save(parser.value("screenshot")) ? 0 : 1);
        });
    }
    return app.exec();
}
