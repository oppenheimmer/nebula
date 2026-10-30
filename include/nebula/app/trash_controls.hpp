#pragma once
#include <QIcon>
#include <QString>
#include <cstddef>

class QAction;
class QToolButton;
class QWidget;

namespace nebula {
enum class TrashScope { Block, Page, Database };
QIcon trash_icon();
void configure_trash_button(QToolButton* button,QAction* action,TrashScope scope);
bool confirm_trash(QWidget* parent,TrashScope scope,const QString& title,
                   const QString& identity,std::size_t contained);
} // namespace nebula
