#include "nebula/app/prototype_window.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/storage/workspace.hpp"
#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"

#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTimer>
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
    parser.addOption({"smoke-test", "Create the editor and exit after event processing."});
    parser.addOption({"benchmark", "Measure single or separate block editors.", "mode"});
    parser.addOption({"blocks", "Number of benchmark blocks (1–10000).", "count", "1000"});
    parser.addOption({"screenshot", "Save an editor screenshot and exit.", "path"});
    parser.addOption({"workspace", "Open or create a workspace in this folder.", "directory"});
    parser.addOption({"workspace-display", "Display the host path of a mounted workspace.", "directory"});
    parser.addOption({"select-workspace", "Choose a folder and print its path; do not create files."});
    parser.addOption({"demo", "Open the sample editor without a workspace."});
    parser.addOption({"compare-editors", "Show the separate-widget investigation tab."});
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
    auto user_home = qEnvironmentVariable("NEBULA_USER_HOME");
    if (user_home.isEmpty())
        user_home = QDir::homePath();
    QApplication::setQuitOnLastWindowClosed(false);
    if (parser.isSet("select-workspace")) {
        WorkspaceDialog dialog(user_home);
        if (parser.isSet("workspace"))
            dialog.set_directory(parser.value("workspace"));
        if (dialog.exec() == QDialog::Accepted)
            std::puts(dialog.directory().toUtf8().constData());
        return 0;
    }
    const bool automated = parser.isSet("smoke-test") || parser.isSet("screenshot");
    std::optional<Workspace> workspace;
    QString display;
    if (parser.isSet("workspace")) {
        try {
            const auto directory = normalize_workspace_directory(parser.value("workspace"), user_home);
            workspace.emplace(Workspace::open(std::filesystem::u8path(directory.toUtf8().constData())));
            display = parser.isSet("workspace-display") ? parser.value("workspace-display") : directory;
        } catch (const std::exception& error) {
            if (automated)
                std::fprintf(stderr, "%s\n", error.what());
            else
                QMessageBox::critical(nullptr, "Cannot open workspace", QString::fromUtf8(error.what()));
            return 1;
        }
    } else if (!automated && !parser.isSet("demo")) {
        WorkspaceDialog dialog(user_home);
        for (;;) {
            if (dialog.exec() != QDialog::Accepted)
                return 0;
            try {
                workspace.emplace(Workspace::open(std::filesystem::u8path(dialog.directory().toUtf8().constData())));
                display = dialog.directory();
                break;
            } catch (const std::exception& error) {
                dialog.show_error(QString::fromUtf8(error.what()));
            }
        }
    }
    PrototypeWindow window(sample_page(), display, parser.isSet("compare-editors"));
    window.show();
    QApplication::setQuitOnLastWindowClosed(true);
    if (parser.isSet("smoke-test"))
        QTimer::singleShot(50, &app, &QApplication::quit);
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(100, &app, [&] {
            app.exit(window.grab().save(parser.value("screenshot")) ? 0 : 1);
        });
    }
    return app.exec();
}
