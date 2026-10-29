#pragma once

#include <QDialog>
#include <functional>

class QLineEdit;
class QLabel;

namespace nebula {

QString default_workspace_directory(const QString& user_home);
QString normalize_workspace_directory(const QString& input, const QString& user_home);

class WorkspaceDialog : public QDialog {
    Q_OBJECT
public:
    using DirectoryPicker = std::function<QString(const QString&)>;
    explicit WorkspaceDialog(const QString& user_home, QWidget* parent = nullptr,
                             DirectoryPicker picker = {});
    QString directory() const;
    void set_directory(const QString& directory);
    void show_error(const QString& message);
    void accept() override;

private:
    QString user_home_;
    QLineEdit* directory_;
    QLabel* error_;
    DirectoryPicker picker_;
};

} // namespace nebula
