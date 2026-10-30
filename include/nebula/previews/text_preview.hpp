#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

namespace nebula {

struct TextPreview {
    QString title, excerpt;
    bool empty() const { return title.isEmpty() && excerpt.isEmpty(); }
};

// Extracts bounded plain text. No script execution or resource loading.
TextPreview extract_text_preview(const QByteArray& bytes, const QByteArray& content_type);
bool preview_url(const QString& text);

struct PreviewLimits {
    int timeout_ms = 5000;
    qint64 bytes = 512 * 1024;
    int redirects = 3;
};

// One explicit paste operation. Opening a note never starts a fetch.
class TextPreviewFetcher : public QObject {
    Q_OBJECT
public:
    explicit TextPreviewFetcher(QObject* parent = nullptr, PreviewLimits limits = {});
    ~TextPreviewFetcher() override;
    void start(const QString& url);
    void cancel();
signals:
    void finished(const nebula::TextPreview& preview, const QString& error);
private:
    PreviewLimits limits_;
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QUrl original_;
    QByteArray body_;
    qint64 received_ = 0;
    int redirects_ = 0;
    bool active_ = false;
    void request(const QUrl& url);
    void read();
    void complete(const QString& error = {});
};
} // namespace nebula

Q_DECLARE_METATYPE(nebula::TextPreview)
