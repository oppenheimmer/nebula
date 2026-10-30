#include "nebula/app/prototype_window.hpp"
#include "nebula/app/termination.hpp"
#include "nebula/app/workspace_dialog.hpp"
#include "nebula/app/workspace_window.hpp"
#include "nebula/editor/block_widgets.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/storage/workspace.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextBlock>
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
    QJsonObject result{{"mode", mode},
                       {"blocks", count},
                       {"qt", qVersion()},
                       {"load_ms", load_ms},
                       {"typing_p95_ms", latencies[28] / 1000000.0},
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

qint64 peak_rss_kib()
{
    QFile status("/proc/self/status");
    if (status.open(QIODevice::ReadOnly))
        for (const auto& line : status.readAll().split('\n'))
            if (line.startsWith("VmHWM:"))
                return QString::fromUtf8(line).simplified().split(' ').value(1).toLongLong();
    return -1;
}

double percentile95(std::vector<qint64> samples)
{
    std::sort(samples.begin(), samples.end());
    return samples.empty() ? 0.0 : samples[std::min(samples.size() - 1, samples.size() * 95 / 100)] / 1000000.0;
}

// Integrated measurements (SUG-33): the real window, autosave path, SQLite,
// search indexing, default labels and image cards over representative fixtures.
int workspace_benchmark()
{
    QTemporaryDir folder;
    const auto root = std::filesystem::u8path(folder.filePath("workspace").toStdString());
    QElapsedTimer timer;
    QJsonObject result{{"mode", "workspace"}, {"qt", qVersion()}};
    std::string long_id, hub_id, images_id, journal_id;
    {
        auto w = Workspace::open(root);
        timer.start();
        w.transaction([&] {
            const auto notes = w.create_database("Notes"), journal = w.create_journal("Journal");
            journal_id = journal.id;
            std::vector<Block> blocks;
            for (int i = 0; i < 10000; ++i)
                blocks.push_back({"long-" + std::to_string(i),
                                  "",
                                  i % 25 ? BlockKind::Paragraph : BlockKind::Heading,
                                  {{"Paragraph " + std::to_string(i) +
                                    ": a local document keeps ideas connected while it wraps."}}});
            long_id = w.create_page(notes.id, "Long page", {}, blocks).document.id;
            hub_id = w.create_page(notes.id, "Project hub", {}, {{"hub-1", "", BlockKind::Paragraph, {{"Hub body"}}}})
                         .document.id;
            const auto date = w.properties(journal.id)[0].id;
            for (int i = 0; i < 5000; ++i) {
                std::vector<Block> entry{{"entry-" + std::to_string(i),
                                          "",
                                          BlockKind::Paragraph,
                                          {{"Diary entry " + std::to_string(i) + " "}}}};
                if (i < 1000)
                    entry[0].runs.push_back(
                        {"hub", false, false, Reference{ReferenceKind::Page, hub_id, {}, {}, false}});
                const auto day = QDate(2012, 1, 1).addDays(i).toString(Qt::ISODate).toStdString();
                w.create_page(journal.id, day, {{date, Date{day}}}, entry);
            }
            std::vector<Block> labelled;
            for (int i = 0; i < 200; ++i)
                labelled.push_back({"label-" + std::to_string(i),
                                    "",
                                    BlockKind::Paragraph,
                                    {{"see ", false, false},
                                     {"x", false, false, Reference{ReferenceKind::Page, hub_id, {}, {}, false}}}});
            images_id = w.create_page(notes.id, "Images", {}, labelled).document.id;
        });
        result["fixture_ms"] = timer.elapsed();
        QImage picture(640, 480, QImage::Format_RGB32);
        picture.fill(QColor("#3070a0"));
        const auto image = folder.filePath("picture.png");
        picture.save(image);
        timer.restart();
        auto page = w.load_page(images_id);
        for (int i = 0; i < 200; ++i)
            w.import_attachment(page, page.document.blocks[std::size_t(i * 2)].id,
                                std::filesystem::u8path(image.toStdString()), true);
        result["import_200_images_ms"] = timer.elapsed();
    }
    timer.restart();
    auto w = Workspace::open(root);
    result["workspace_open_ms"] = timer.elapsed();
    timer.restart();
    WorkspaceWindow window(w);
    window.resize(1300, 850);
    window.show();
    QApplication::processEvents();
    result["window_ms"] = timer.elapsed();
    auto* editor = window.page_editor();
    auto type = [&](const char* name, const std::string& page, const QString& block) {
        timer.restart();
        window.open_page(page);
        QApplication::processEvents();
        result[QString(name) + "_open_ms"] = timer.elapsed();
        editor->go_to_block(block);
        std::vector<qint64> latencies;
        for (int i = 0; i < 40; ++i) {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
            timer.restart();
            QApplication::sendEvent(editor, &key);
            QApplication::processEvents();
            latencies.push_back(timer.nsecsElapsed());
        }
        result[QString(name) + "_typing_p95_ms"] = percentile95(latencies);
        timer.restart();
        window.flush();
        result[QString(name) + "_save_ms"] = timer.elapsed();
        timer.restart();
        editor->move_current(1);
        QApplication::processEvents();
        result[QString(name) + "_move_ms"] = timer.elapsed();
        window.flush();
    };
    type("long_page", long_id, "long-5000");
    type("hub_page", hub_id, "hub-1");
    type("images_page", images_id, "label-100");
    // Renaming the hub changes the default label shown by 1,200 referring blocks.
    window.open_page(hub_id);
    auto* title = window.findChild<QLineEdit*>("pageTitle");
    title->setText("Project hub renamed");
    emit title->textEdited(title->text());
    timer.restart();
    window.flush();
    result["hub_rename_save_ms"] = timer.elapsed();
    auto* databases = window.findChild<QComboBox*>("databases");
    timer.restart();
    databases->setCurrentIndex(databases->findData(QString::fromStdString(journal_id)));
    QApplication::processEvents();
    result["journal_table_5000_ms"] = timer.elapsed();
    timer.restart();
    const auto hits = w.search("diary entry");
    result["search_ms"] = timer.elapsed();
    result["search_hits"] = int(hits.size());
    result["peak_rss_kib"] = peak_rss_kib();
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return 0;
}

void show_error(const QString& title, const QString& text)
{
    QMessageBox box(QMessageBox::Critical, title, text, QMessageBox::Ok);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Nebula editor prototype");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"smoke-test", "Create the editor and exit after event processing."});
    parser.addOption({"benchmark", "Measure single or separate block editors, or the integrated workspace.", "mode"});
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
        if (mode == "workspace")
            return workspace_benchmark();
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
                show_error("Cannot open workspace", QString::fromUtf8(error.what()));
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
    std::unique_ptr<PrototypeWindow> window;
    try {
        if (workspace)
            window = std::make_unique<WorkspaceWindow>(*workspace, display);
        else
            window = std::make_unique<PrototypeWindow>(sample_page(), display, parser.isSet("compare-editors"));
    } catch (const std::exception& error) {
        if (automated)
            std::fprintf(stderr, "%s\n", error.what());
        else
            show_error("Cannot load workspace", QString::fromUtf8(error.what()));
        return 1;
    }
    window->show();
    QApplication::setQuitOnLastWindowClosed(true);
    // docker stop, kill and logout signals flush pending edits instead of dropping them.
    install_termination_handler([](int) {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* open = qobject_cast<WorkspaceWindow*>(widget))
                open->flush();
        QApplication::quit();
    });
    if (parser.isSet("smoke-test"))
        QTimer::singleShot(50, &app, &QApplication::quit);
    if (parser.isSet("screenshot")) {
        QTimer::singleShot(100, &app, [&] { app.exit(window->grab().save(parser.value("screenshot")) ? 0 : 1); });
    }
    return app.exec();
}
