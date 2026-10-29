#include "nebula/editor/block_widgets.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace nebula {

BlockWidgets::BlockWidgets(QWidget* parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setAccessibleName("Separate block editor comparison");
}

void BlockWidgets::set_page(const Page& page)
{
    validate(page);
    editors_.clear();
    delete takeWidget();
    auto* body = new QWidget;
    auto* layout = new QVBoxLayout(body);
    layout->addWidget(new QLabel("Comparison: selection and undo belong to each block widget.", body));
    for (const auto& block : page.blocks) {
        auto* editor = new QTextEdit(body);
        editor->setAccessibleName("Block " + QString::fromStdString(block.id));
        QString text;
        for (const auto& run : block.runs)
            text += QString::fromStdString(run.text);
        if (!block.asset_label.empty())
            text = "[" + QString::fromStdString(block.asset_label) + "]";
        editor->setPlainText(text);
        editor->document()->clearUndoRedoStacks();
        editor->setMinimumHeight(72);
        editor->setMaximumHeight(110);
        layout->addWidget(editor);
        editors_.push_back(editor);
    }
    layout->addStretch();
    setWidget(body);
}

} // namespace nebula
