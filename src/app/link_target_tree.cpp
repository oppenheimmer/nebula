#include "nebula/app/link_target_tree.hpp"
#include "nebula/qt/utf8.hpp"
#include <QPersistentModelIndex>
#include <QTimer>
#include <algorithm>
#include <unordered_map>

namespace nebula {
namespace {
constexpr int PageIndex = Qt::UserRole + 3;
constexpr int More = Qt::UserRole + 4;
QString context(const LinkTargetRow& row)
{
    return utf8::decode(row.database_name) + " / " + utf8::decode(row.page_title) + " [" + utf8::decode(row.page_id) + "]";
}
QTreeWidgetItem* continuation(QTreeWidgetItem* parent, QTreeWidget* tree, const QString& text)
{
    auto* result = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    result->setText(0, text);
    result->setData(0, More, true);
    result->setToolTip(0, "Press Enter or double-click to show the next 100 targets");
    return result;
}
} // namespace
LinkTargetTree::LinkTargetTree(std::vector<LinkTargetRow> rows, QWidget* parent)
    : QTreeWidget(parent), rows_(std::move(rows))
{
    setObjectName("linkTargets");
    setHeaderLabel("Choose a specific target");
    setAccessibleName("Pages, headings and blocks");
    std::unordered_map<std::string, std::size_t> positions;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind == ReferenceKind::Page) {
            positions.emplace(rows_[i].id, pages_.size());
            pages_.push_back({i, {}, {}, 0});
        }
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].kind == ReferenceKind::Block) {
            const auto found = positions.find(rows_[i].page_id);
            if (found != positions.end()) pages_[found->second].blocks.push_back(i);
        }
    }
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* page) {
        if (page->data(0, PageIndex).isValid() && !page->childCount()) more_blocks(page);
    });
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* row) { activate_more(row); });
    search({});
}
QTreeWidgetItem* LinkTargetTree::item(std::size_t index, QTreeWidgetItem* parent)
{
    const auto& row = rows_[index];
    auto* result = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(this);
    const auto label = row.kind == ReferenceKind::Page ? context(row)
        : (row.heading ? "Heading: " : "Block: ") + utf8::decode(row.label).left(100) + " [" + utf8::decode(row.id) + "]";
    result->setText(0, label);
    result->setData(0, Qt::UserRole, utf8::decode(row.id));
    result->setData(0, Qt::UserRole + 1, int(row.kind));
    result->setData(0, Qt::UserRole + 2, utf8::decode(row.label));
    return result;
}
void LinkTargetTree::search(const QString& query)
{
    clear();
    shown_ = 0;
    matches_.clear();
    const auto words = query.split(' ', Qt::SkipEmptyParts);
    const auto matches = [&](const QString& text) {
        return std::all_of(words.begin(), words.end(), [&](const auto& word) { return text.contains(word, Qt::CaseInsensitive); });
    };
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        auto& page = pages_[i];
        page.matches.clear();
        page.shown = 0;
        const auto title = context(rows_[page.row]);
        for (const auto index : page.blocks) {
            const auto& block = rows_[index];
            if (matches(title + " " + (block.heading ? "Heading: " : "Block: ") + utf8::decode(block.label) + " [" + utf8::decode(block.id) + "]"))
                page.matches.push_back(index);
        }
        if (matches(title) || !page.matches.empty()) matches_.push_back(i);
    }
    more_pages();
}
void LinkTargetTree::more_pages()
{
    const auto end = std::min(matches_.size(), shown_ + Batch);
    while (shown_ < end) {
        const auto index = matches_[shown_++];
        const auto& page = pages_[index];
        auto* row = item(page.row);
        row->setData(0, PageIndex, qulonglong(index));
        if (!page.matches.empty()) row->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    }
    if (shown_ < matches_.size()) continuation(nullptr, this, "Show more pages…");
}
void LinkTargetTree::more_blocks(QTreeWidgetItem* row)
{
    auto& page = pages_.at(row->data(0, PageIndex).toULongLong());
    const auto end = std::min(page.matches.size(), page.shown + Batch);
    while (page.shown < end) item(page.matches[page.shown++], row);
    if (page.shown < page.matches.size()) continuation(row, this, "Show more blocks…");
}
void LinkTargetTree::activate_more(QTreeWidgetItem* row)
{
    if (!row || !row->data(0, More).toBool()) return;
    // Search may rebuild the tree before this callback runs. A persistent index
    // also makes repeated activation harmless after the first batch is loaded.
    const QPersistentModelIndex index = indexFromItem(row);
    QTimer::singleShot(0, this, [this, index] {
        if (!index.isValid()) return;
        auto* current = itemFromIndex(index);
        auto* parent = current->parent();
        const int next = parent ? parent->indexOfChild(current) : indexOfTopLevelItem(current);
        delete current;
        if (parent) more_blocks(parent); else more_pages();
        setCurrentItem(parent ? parent->child(next) : topLevelItem(next));
    });
}
} // namespace nebula
