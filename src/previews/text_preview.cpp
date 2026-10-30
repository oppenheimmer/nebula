#include "nebula/previews/text_preview.hpp"
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextDocument>
#include <algorithm>

namespace nebula {
namespace {
class TextOnlyDocument final : public QTextDocument {
    QVariant loadResource(int, const QUrl&) override { return {}; }
};
QString clean(const QString& input, int maximum)
{
    QString result;
    for (const auto character : input) {
        if (character.isSpace()) result += ' ';
        else if (character.category() != QChar::Other_Control
                 && character.category() != QChar::Other_Format
                 && character != QChar::ObjectReplacementCharacter) result += character;
    }
    result = result.simplified();
    if (result.size() > maximum) {
        result.truncate(maximum);
        if (!result.isEmpty() && result.at(result.size() - 1).isHighSurrogate()) result.chop(1);
        result += QChar(0x2026);
    }
    return result;
}
QString html_text(const QString& html)
{
    TextOnlyDocument document;
    document.setHtml(html);
    return document.toPlainText();
}
void metadata(const QString& html, TextPreview& preview)
{
    // Scan once, including malformed tags; a missing closing '>' must not make
    // every later '<meta' rescan the rest of a large response.
    qsizetype position = 0;
    while ((position = html.indexOf('<', position)) >= 0) {
        const auto start = position++;
        QChar quote;
        while (position < html.size()) {
            const auto c = html.at(position);
            if (!quote.isNull()) { if (c == quote) quote = {}; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '<' || c == '>') break;
            ++position;
        }
        if (position == html.size()) break;
        if (html.at(position) == '<') continue;
        const auto tag = html.mid(start + 1, position - start - 1); ++position;
        if (tag.size() < 5 || tag.size() > 8192 || tag.left(4).compare("meta", Qt::CaseInsensitive) || !tag.at(4).isSpace()) continue;
        QString name, content;
        qsizetype i = 4;
        while (i < tag.size()) {
            while (i < tag.size() && tag.at(i).isSpace()) ++i;
            const auto key_start = i;
            while (i < tag.size() && (tag.at(i).isLetterOrNumber() || QString("_-:").contains(tag.at(i)))) ++i;
            if (i == key_start) { ++i; continue; }
            const auto key = tag.mid(key_start, i - key_start).toLower();
            while (i < tag.size() && tag.at(i).isSpace()) ++i;
            if (i == tag.size() || tag.at(i) != '=') continue;
            ++i; while (i < tag.size() && tag.at(i).isSpace()) ++i;
            if (i == tag.size()) break;
            const auto delimiter = tag.at(i);
            const bool quoted = delimiter == '\'' || delimiter == '"';
            if (quoted) ++i;
            const auto value_start = i;
            while (i < tag.size() && (quoted ? tag.at(i) != delimiter : !tag.at(i).isSpace())) ++i;
            const auto value = tag.mid(value_start, i - value_start);
            if (quoted && i < tag.size()) ++i;
            if (key == "name" || key == "property") name = value.toLower();
            if (key == "content") content = value;
        }
        if (name == "og:title" && preview.title.isEmpty()) preview.title = html_text(content);
        if ((name == "description" || name == "og:description") && preview.excerpt.isEmpty()) preview.excerpt = html_text(content);
    }
}
bool same_origin(const QUrl& a, const QUrl& b)
{
    return a.scheme() == b.scheme() && a.host() == b.host()
        && a.port(a.scheme() == "https" ? 443 : 80) == b.port(b.scheme() == "https" ? 443 : 80);
}
QByteArray mime_type(const QByteArray& header) { return header.split(';').first().trimmed().toLower(); }
} // namespace

bool preview_url(const QString& text)
{
    if (text.size() > 8192 || text != text.trimmed()) return false;
    for (const auto character : text)
        if (character.isSpace() || character.category() == QChar::Other_Control) return false;
    const QUrl url(text, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty()
        && (url.scheme() == "http" || url.scheme() == "https");
}

TextPreview extract_text_preview(const QByteArray& bytes, const QByteArray& content_type)
{
    if (bytes.size() > 512 * 1024) return {};
    const auto type = mime_type(content_type);
    if (type != "text/html" && type != "application/xhtml+xml" && type != "text/plain") return {};
    std::optional<QStringConverter::Encoding> encoding = type == "text/plain"
        ? QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8)
        : QStringConverter::encodingForHtml(bytes);
    const QRegularExpression charset("charset\\s*=\\s*[\"']?([a-zA-Z0-9_-]+)", QRegularExpression::CaseInsensitiveOption);
    const auto match = charset.match(QString::fromLatin1(content_type));
    if (match.hasMatch()) encoding = QStringConverter::encodingForName(match.captured(1).toLatin1().constData());
    if (!encoding) return {};
    QStringDecoder decoder(*encoding, QStringConverter::Flag::Stateless);
    const QString text = decoder(bytes);
    if (decoder.hasError()) return {};
    if (type == "text/plain") return {{}, clean(text, 400)};

    TextOnlyDocument document;
    document.setHtml(text);
    TextPreview preview{document.metaInformation(QTextDocument::DocumentTitle), {}};
    metadata(text, preview);
    if (preview.excerpt.isEmpty()) preview.excerpt = document.toPlainText();
    preview.title = clean(preview.title, 160);
    preview.excerpt = clean(preview.excerpt, 400);
    return preview;
}

TextPreviewFetcher::TextPreviewFetcher(QObject* parent, PreviewLimits limits)
    : QObject(parent), limits_(limits)
{
    manager_.setProxy(QNetworkProxy::NoProxy);
    connect(&manager_, &QNetworkAccessManager::authenticationRequired, this,
            [this] { complete("The page requires authentication."); });
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { complete("Link preview timed out."); });
}
void TextPreviewFetcher::start(const QString& url)
{
    cancel();
    active_ = true; body_.clear(); received_ = 0; redirects_ = 0;
    if (!preview_url(url)) { complete("Only complete http(s) URLs without credentials can be previewed."); return; }
    original_ = QUrl(url, QUrl::StrictMode); original_.setFragment({});
    deadline_.start(std::clamp(limits_.timeout_ms, 1, 30000));
    request(original_);
}
TextPreviewFetcher::~TextPreviewFetcher()
{
    deadline_.stop();
    if (reply_) { reply_->disconnect(this); reply_->abort(); }
}
void TextPreviewFetcher::cancel()
{
    if (active_) complete("Link preview cancelled.");
}
void TextPreviewFetcher::request(const QUrl& url)
{
    QNetworkRequest network_request(url);
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    network_request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    network_request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    network_request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    network_request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    network_request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    network_request.setRawHeader("Accept", "text/html, application/xhtml+xml, text/plain");
    network_request.setRawHeader("Accept-Encoding", "identity");
    network_request.setRawHeader("User-Agent", "Nebula link preview");
    auto* reply = manager_.get(network_request); reply_ = reply;
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, &TextPreviewFetcher::read);
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > std::clamp<qint64>(limits_.bytes, 1, 512 * 1024) - received_) complete("The page exceeds the preview size limit.");
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        read();
        if (!active_ || reply_ != reply) return;
        const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (target.isValid()) {
            const auto next = reply->url().resolved(target.toUrl());
            if (!preview_url(next.toString()) || !same_origin(original_, next)
                || ++redirects_ > std::clamp(limits_.redirects, 0, 3)) {
                complete("The page redirects outside the preview limits."); return;
            }
            reply->disconnect(this); reply->deleteLater(); reply_ = nullptr; body_.clear();
            request(next); return;
        }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            complete("The page could not be retrieved."); return;
        }
        complete();
    });
}
void TextPreviewFetcher::read()
{
    if (!active_ || !reply_) return;
    const auto limit = std::clamp<qint64>(limits_.bytes, 1, 512 * 1024);
    const auto bytes = reply_->read(limit - received_ + 1);
    received_ += bytes.size();
    if (received_ > limit) { complete("The page exceeds the preview size limit."); return; }
    body_ += bytes;
}
void TextPreviewFetcher::complete(const QString& error)
{
    if (!active_) return;
    active_ = false; deadline_.stop();
    TextPreview preview;
    if (error.isEmpty() && reply_) preview = extract_text_preview(body_, reply_->rawHeader("Content-Type"));
    if (reply_) {
        reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); reply_ = nullptr;
    }
    body_.clear();
    emit finished(preview, error.isEmpty() && preview.empty() ? "The page contained no supported preview text." : error);
}
} // namespace nebula
