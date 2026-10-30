#pragma once

#include "nebula/previews/text_preview.hpp"
#include <QTextCursor>
#include <optional>

class QStatusBar;
class QWidget;

namespace nebula {
class PageEditor;

// The application calls begin only after a complete URL has been pasted. The
// fetcher stays separate from the editor and from canonical document storage.
class PastedLinkPreview : public QObject {
    Q_OBJECT
public:
    PastedLinkPreview(PageEditor* editor, QStatusBar* status, QObject* parent = nullptr, PreviewLimits limits = {});
    ~PastedLinkPreview() override;
    bool running() const { return active_; }
public slots:
    void begin(const QString& url, const QString& source_block);
    void cancel();
signals:
    void preview_inserted(const QString& block_id);
    void preview_finished(const QString& error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<PageEditor> editor_;
    QPointer<QStatusBar> status_;
    QPointer<QWidget> controls_;
    QPointer<TextPreviewFetcher> fetcher_;
    QTextCursor bookmark_;
    QString page_, block_, url_;
    QTimer check_;
    PreviewLimits limits_;
    std::optional<TextPreview> ready_;
    quint64 generation_ = 0;
    bool active_ = false, composing_ = false;
    bool valid_bookmark() const;
    void insert_ready();
    void finish(const QString& error, bool report = true);
};
} // namespace nebula
