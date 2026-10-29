#pragma once

#include "nebula/core/document.hpp"

#include <QScrollArea>
#include <QTextEdit>
#include <vector>

namespace nebula {

// Comparison baseline: one native editor per block. Deliberately exposes native
// per-widget selection/undo boundaries for the feasibility tests.
class BlockWidgets : public QScrollArea {
    Q_OBJECT
public:
    explicit BlockWidgets(QWidget* parent = nullptr);
    void set_page(const Page& page);
    const std::vector<QTextEdit*>& editors() const { return editors_; }

private:
    std::vector<QTextEdit*> editors_;
};

} // namespace nebula
