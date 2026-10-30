#pragma once
#include "nebula/storage/workspace.hpp"
#include <QTreeWidget>

namespace nebula {
// Widget items are allocated only for visible batches. Search still examines
// every cached row, so targets beyond the first batch remain discoverable.
class LinkTargetTree : public QTreeWidget {
public:
    explicit LinkTargetTree(std::vector<LinkTargetRow> rows, QWidget* parent = nullptr);
    void search(const QString& query);

private:
    struct Page {
        std::size_t row;
        std::vector<std::size_t> blocks, matches;
        std::size_t shown = 0;
    };
    static constexpr std::size_t Batch = 100;
    std::vector<LinkTargetRow> rows_;
    std::vector<Page> pages_;
    std::vector<std::size_t> matches_;
    std::size_t shown_ = 0;
    QTreeWidgetItem* item(std::size_t row, QTreeWidgetItem* parent = nullptr);
    void more_pages();
    void more_blocks(QTreeWidgetItem* page);
    void activate_more(QTreeWidgetItem* item);
};
} // namespace nebula
