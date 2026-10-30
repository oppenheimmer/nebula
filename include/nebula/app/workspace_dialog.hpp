#pragma once

#include <QDialog>
#include <functional>

class QLineEdit;
class QLabel;

namespace nebula {

QString default_workspace_directory(const QString& user_home);
QString normalize_workspace_directory(const QString& input, const QString& user_home);
// Why a chosen folder needs attention: a backup or export must not be edited in
// place, and a non-empty unrelated folder should be confirmed first.
enum class FolderKind { New, Workspace, Backup, Export, Other };
FolderKind classify_workspace_folder(const QString& directory);

class WorkspaceDialog : public QDialog {
    Q_OBJECT
public:
    using DirectoryPicker = std::function<QString(const QString&)>;
    using Confirmer = std::function<bool(const QString& directory)>;
    explicit WorkspaceDialog(const QString& user_home, QWidget* parent = nullptr,
                             DirectoryPicker picker = {}, Confirmer confirm_other = {});
    QString directory() const;
    void set_directory(const QString& directory);
    void show_error(const QString& message);
    void accept() override;

private:
    QString user_home_;
    QLineEdit* directory_;
    QLabel* error_;
    DirectoryPicker picker_;
    Confirmer confirm_other_;
};

} // namespace nebula
