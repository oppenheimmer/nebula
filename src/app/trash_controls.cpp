#include "nebula/app/trash_controls.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace nebula {
namespace {
QString scope_name(TrashScope scope)
{
    switch (scope) {
    case TrashScope::Block:
        return "block";
    case TrashScope::Page:
        return "page";
    case TrashScope::Database:
        return "database";
    }
    return {};
}
} // namespace
QIcon trash_icon()
{
    QIcon icon;
    for (const int size : {16, 24, 32, 48}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(QColor("#c63c3c"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(4, 6, 20, 6);
        painter.drawLine(9, 3, 15, 3);
        painter.drawLine(9, 3, 8, 6);
        painter.drawLine(15, 3, 16, 6);
        painter.drawLine(6, 8, 7, 21);
        painter.drawLine(7, 21, 17, 21);
        painter.drawLine(17, 21, 18, 8);
        painter.drawLine(10, 10, 10, 17);
        painter.drawLine(14, 10, 14, 17);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}
void configure_trash_button(QToolButton* button, QAction* action, TrashScope scope)
{
    action->setIcon(trash_icon());
    action->setToolTip("Move " + scope_name(scope) + " to Trash…");
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    button->setIconSize(QSize(18, 18));
    button->setMinimumSize(28, 28);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setAccessibleName(action->toolTip());
    button->setAccessibleDescription(
        "Opens a confirmation. Content can be restored; this does not permanently delete it.");
}
bool confirm_trash(QWidget* parent, TrashScope scope, const QString& title, const QString& identity,
                   std::size_t contained)
{
    const auto name = scope_name(scope);
    QDialog dialog(parent);
    dialog.setWindowTitle("Move " + name + " to Trash?");
    dialog.setObjectName(scope == TrashScope::Database ? "confirmTrashDatabase"
                         : scope == TrashScope::Page   ? "confirmTrashPage"
                                                       : "confirmTrashBlock");
    auto* layout = new QVBoxLayout(&dialog);
    auto label = [&](const QString& text) {
        auto* value = new QLabel(text, &dialog);
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(value);
        return value;
    };
    auto* target = label(title.isEmpty() ? "Untitled " + name : title.left(160));
    target->setObjectName("trashTarget");
    target->setToolTip(title);
    label("ID: " + identity);
    QString description;
    if (scope == TrashScope::Database)
        description = QString("This database contains %1 pages. Its pages, views and properties will be hidden with "
                              "it. Pages already in Trash keep their own status.")
                          .arg(qulonglong(contained));
    else if (scope == TrashScope::Page)
        description = QString("This page contains %1 blocks. Its blocks and properties will be moved with the page.")
                          .arg(qulonglong(contained));
    else
        description =
            QString(
                "Move this block and its nested blocks to Trash (%1 blocks in total). Other blocks stay in the page.")
                .arg(qulonglong(contained));
    label(description)->setObjectName("trashScope");
    label(scope == TrashScope::Block
              ? "You can undo this change while this page is open, or use Restore after it saves. Attachment files are "
                "retained."
              : "Use Restore to recover this " + name + ". Content, identities and attachment files are retained.");
    label("This action does not permanently delete anything.");
    QCheckBox* acknowledgement = nullptr;
    if (scope == TrashScope::Database) {
        acknowledgement =
            new QCheckBox("I understand that this moves the whole database and its pages to Trash.", &dialog);
        acknowledgement->setObjectName("confirmDatabaseTrash");
        acknowledgement->setChecked(false);
        layout->addWidget(acknowledgement);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    auto* confirm = buttons->addButton("Move " + name + " to Trash", QDialogButtonBox::AcceptRole);
    confirm->setObjectName("confirmTrash");
    confirm->setIcon(trash_icon());
    confirm->setAutoDefault(false);
    confirm->setEnabled(!acknowledgement);
    auto* cancel = buttons->button(QDialogButtonBox::Cancel);
    cancel->setObjectName("cancelTrash");
    cancel->setDefault(true);
    cancel->setFocus();
    if (acknowledgement)
        QObject::connect(acknowledgement, &QCheckBox::toggled, confirm, &QPushButton::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(490, dialog.sizeHint().height());
    return dialog.exec() == QDialog::Accepted && (!acknowledgement || acknowledgement->isChecked());
}
} // namespace nebula
