#include "nebula/app/workspace_dialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <stdexcept>

namespace nebula {

QString default_workspace_directory(const QString& user_home)
{
    return QDir(user_home).filePath("Document/nebula");
}

QString normalize_workspace_directory(const QString& input, const QString& user_home)
{
    auto path = input.trimmed();
    if (path == "~")
        path = user_home;
    else if (path.startsWith("~/"))
        path = QDir(user_home).filePath(path.mid(2));
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        throw std::invalid_argument("Choose a full folder path, or a path starting with ~/.");
    if (path.contains(QChar::Null) || path.contains('\n') || path.contains('\r'))
        throw std::invalid_argument("The folder path contains an invalid character.");
    const auto clean = QDir::cleanPath(path);
    if (QFileInfo::exists(clean) && !QFileInfo(clean).isDir())
        throw std::invalid_argument("Choose a folder rather than an individual file.");
    return clean;
}

WorkspaceDialog::WorkspaceDialog(const QString& user_home, QWidget* parent, DirectoryPicker picker)
    : QDialog(parent), user_home_(user_home), picker_(std::move(picker))
{
    setWindowTitle("Choose your Nebula workspace");
    setMinimumWidth(580);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel("Where would you like to keep your database?", this);
    layout->addWidget(explanation);
    layout->addWidget(new QLabel("Choose an existing Nebula folder or enter a location for a new one.", this));
    auto* row = new QHBoxLayout;
    directory_ = new QLineEdit(default_workspace_directory(user_home), this);
    directory_->setObjectName("workspaceDirectory");
    directory_->setAccessibleName("Workspace folder");
    row->addWidget(directory_);
    auto* browse = new QPushButton("Browse…", this);
    browse->setObjectName("browseWorkspace");
    row->addWidget(browse);
    layout->addLayout(row);
    auto* details = new QLabel("This folder contains nebula.sqlite3 and an attachments folder.\n"
                              "A new workspace is created only after you choose Open workspace.", this);
    details->setWordWrap(true);
    layout->addWidget(details);
    error_ = new QLabel(this);
    error_->setObjectName("workspaceError");
    error_->setWordWrap(true);
    error_->setStyleSheet("color: #a12622;");
    layout->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Open)->setText("Open workspace");
    buttons->button(QDialogButtonBox::Open)->setObjectName("openWorkspace");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelWorkspace");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &WorkspaceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &WorkspaceDialog::reject);
    connect(directory_, &QLineEdit::textChanged, error_, &QLabel::clear);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto selected = picker_ ? picker_(directory_->text())
            : QFileDialog::getExistingDirectory(this, "Choose workspace folder", directory_->text(),
                                                QFileDialog::ShowDirsOnly | QFileDialog::ReadOnly);
        if (!selected.isEmpty())
            set_directory(selected);
    });
    directory_->selectAll();
}

QString WorkspaceDialog::directory() const { return directory_->text(); }
void WorkspaceDialog::set_directory(const QString& path) { directory_->setText(path); }
void WorkspaceDialog::show_error(const QString& message) { error_->setText(message); }

void WorkspaceDialog::accept()
{
    try {
        directory_->setText(normalize_workspace_directory(directory_->text(), user_home_));
        QDialog::accept();
    } catch (const std::invalid_argument& error) {
        show_error(QString::fromUtf8(error.what()));
    }
}

} // namespace nebula
