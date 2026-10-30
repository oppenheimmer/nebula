#include "nebula/previews/text_preview.hpp"
#include <QNetworkCookieJar>
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <functional>

using namespace nebula;
namespace {
class LocalWeb final : public QObject {
public:
    QTcpServer server;
    QList<QByteArray> requests;
    std::function<void(QTcpSocket*, const QByteArray&)> respond;
    LocalWeb()
    {
        if (!server.listen(QHostAddress::LocalHost)) qFatal("Cannot listen on loopback");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes = QByteArray{}]() mutable {
                    bytes += socket->readAll();
                    if (!bytes.contains("\r\n\r\n")) return;
                    socket->disconnect(SIGNAL(readyRead()));
                    requests.push_back(bytes);
                    if (respond) respond(socket, bytes);
                });
            }
        });
    }
    QString url(const QString& suffix = {}) const
    {
        return QString("http://127.0.0.1:%1/%2").arg(server.serverPort()).arg(suffix);
    }
    static void reply(QTcpSocket* socket, const QByteArray& body,
                      const QByteArray& type = "text/html; charset=utf-8", const QByteArray& extra = {})
    {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body);
        socket->disconnectFromHost();
    }
};
TextPreview value(const QSignalSpy& spy) { return qvariant_cast<TextPreview>(spy.at(0).at(0)); }
QString error(const QSignalSpy& spy) { return spy.at(0).at(1).toString(); }
}

class PreviewTests : public QObject {
    Q_OBJECT
private slots:
    void url_validation();
    void bounded_text_extraction_without_resources();
    void encodings_empty_binary_and_unsupported_responses();
    void one_get_without_credentials_cookies_or_subresources();
    void redirects_stay_on_the_pasted_origin_and_are_bounded();
    void declared_and_streamed_size_limits();
    void timeout_cancel_and_destruction_finish_promptly();
    void http_and_authentication_failures();
    void https_uses_certificate_and_hostname_verification();
};
void PreviewTests::url_validation()
{
    for (const auto* valid : {"https://example.com", "http://127.0.0.1:123/a?query=1#part", "https://example.com/%E6%97%A5"})
        QVERIFY(preview_url(QString::fromLatin1(valid)));
    for (const auto* invalid : {"file:///etc/passwd", "data:text/html,test", "javascript:alert(1)", "ftp://example.com", "https://user:secret@example.com", "https://", "https://example.com/has space", " https://example.com", "https://example.com\n"})
        QVERIFY(!preview_url(QString::fromLatin1(invalid)));
    TextPreviewFetcher fetcher; QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start("file:///etc/passwd"); QCOMPARE(done.size(), 1); QVERIFY(value(done).empty()); QVERIFY(!error(done).isEmpty());
}
void PreviewTests::bounded_text_extraction_without_resources()
{
    const auto preview = extract_text_preview(
        "<html><head><title>Title &amp; text</title><meta content='Extracted &amp; short' name='description'>"
        "<script>do_not_run_or_include()</script><style>body{background:url(http://127.0.0.1/no)}</style></head>"
        "<body><img src='file:///etc/passwd'><h1>Body</h1><p>Visible</p></body></html>", "text/html");
    QCOMPARE(preview.title, QString("Title & text")); QCOMPARE(preview.excerpt, QString("Extracted & short"));
    const auto fallback = extract_text_preview("<h1>Heading</h1><p>Body 日本語</p><script>secret</script><img src='https://example.com/image'>", "text/html");
    QVERIFY(fallback.excerpt.contains(QString::fromUtf8("日本語"))); QVERIFY(!fallback.excerpt.contains("secret"));
    QVERIFY(!fallback.excerpt.contains(QChar::ObjectReplacementCharacter));
    const auto cleaned = extract_text_preview((QString("one\n two ")+QChar(0x202e)+"three"+QChar(0)).toUtf8(), "text/plain");
    QCOMPARE(cleaned.excerpt, QString("one two three"));
    const auto large = extract_text_preview(QByteArray(4000, 'x'), "text/plain");
    QCOMPARE(large.excerpt.size(), 401); QVERIFY(large.excerpt.endsWith(QChar(0x2026)));
    QCOMPARE(extract_text_preview("<meta content='One > two' property='og:description'>", "text/html").excerpt,
             QString("One > two"));
    QCOMPARE(extract_text_preview("<meta name=description content=unquoted>", "text/html").excerpt,
             QString("unquoted"));
    QByteArray malformed("<title>Valid title</title>");
    for (int i = 0; i < 10000; ++i) malformed += "<meta ";
    QCOMPARE(extract_text_preview(malformed, "text/html").title, QString("Valid title"));
}
void PreviewTests::encodings_empty_binary_and_unsupported_responses()
{
    const auto latin = extract_text_preview(QByteArray("caf\xe9"), "text/plain; charset=iso-8859-1");
    QCOMPARE(latin.excerpt, QString::fromUtf8("café"));
    QVERIFY(extract_text_preview("<img src='https://example.com/pic'>", "text/html").empty());
    QVERIFY(extract_text_preview("image bytes", "image/png").empty());
    QVERIFY(extract_text_preview("%PDF", "application/pdf").empty());
    QVERIFY(extract_text_preview(QByteArray("\xff\xfe\x80", 3), "text/plain; charset=utf-8").empty());
    QVERIFY(extract_text_preview(QByteArray("abc\xe3\x81"), "text/plain; charset=utf-8").empty());
    QCOMPARE(extract_text_preview("<meta charset=unsupported-encoding>", "text/plain").excerpt,
             QString("<meta charset=unsupported-encoding>"));
    QVERIFY(extract_text_preview("body", "text/plain; charset=unsupported-encoding").empty());
    QVERIFY(extract_text_preview(QByteArray(512*1024+1, 'x'), "text/plain").empty());
}
void PreviewTests::one_get_without_credentials_cookies_or_subresources()
{
    LocalWeb web;
    web.respond = [](auto* socket, const auto&) {
        LocalWeb::reply(socket, "<title>Preview</title><p>Text only</p><img src='/picture'><iframe src='/frame'></iframe>",
                        "text/html", "Set-Cookie: secret=discard; Path=/\r\n");
    };
    TextPreviewFetcher fetcher; QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start(web.url("note#fragment")); QTRY_COMPARE(done.size(), 1);
    QVERIFY(error(done).isEmpty()); QCOMPARE(value(done).title, QString("Preview"));
    QCOMPARE(web.requests.size(), 1); const auto request = web.requests[0].toLower();
    QVERIFY(request.startsWith("get /note http/1.1\r\n"));
    QVERIFY(!request.contains("\r\ncookie:")); QVERIFY(!request.contains("\r\nauthorization:")); QVERIFY(!request.contains("\r\nreferer:"));
    done.clear(); fetcher.start(web.url("again")); QTRY_COMPARE(done.size(), 1);
    QCOMPARE(web.requests.size(), 2); QVERIFY(!web.requests[1].toLower().contains("\r\ncookie:"));
}
void PreviewTests::redirects_stay_on_the_pasted_origin_and_are_bounded()
{
    LocalWeb web, other;
    other.respond = [](auto* socket, const auto&) { LocalWeb::reply(socket, "private endpoint", "text/plain"); };
    web.respond = [&](auto* socket, const auto& request) {
        const auto destination = request.startsWith("GET /outside ") ? other.url().toUtf8() : QByteArray("/landing");
        if (request.startsWith("GET /landing ")) LocalWeb::reply(socket, "redirected text", "text/plain");
        else { socket->write("HTTP/1.1 302 Found\r\nLocation: " + destination + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); }
    };
    TextPreviewFetcher fetcher; QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QCOMPARE(value(done).excerpt, QString("redirected text")); QCOMPARE(web.requests.size(), 2);
    done.clear(); fetcher.start(web.url("outside")); QTRY_COMPARE(done.size(), 1); QVERIFY(!error(done).isEmpty()); QVERIFY(other.requests.isEmpty());
    web.respond = [](auto* socket, const auto&) {
        socket->write("HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost();
    };
    web.requests.clear(); done.clear(); fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1);
    QVERIFY(!error(done).isEmpty()); QCOMPARE(web.requests.size(), 4);
}
void PreviewTests::declared_and_streamed_size_limits()
{
    LocalWeb web;
    web.respond = [](auto* socket, const auto&) { LocalWeb::reply(socket, QByteArray(5000, 'x'), "text/plain"); };
    TextPreviewFetcher fetcher(nullptr, {1000, 1024, 3}); QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QVERIFY(error(done).contains("size limit")); QVERIFY(value(done).empty());
    web.respond = [](auto* socket, const auto&) {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n" + QByteArray(5000, 'x')); socket->disconnectFromHost();
    };
    done.clear(); fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QVERIFY(error(done).contains("size limit"));
}
void PreviewTests::timeout_cancel_and_destruction_finish_promptly()
{
    LocalWeb web;
    TextPreviewFetcher fetcher(nullptr, {50, 1024, 3}); QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QVERIFY(error(done).contains("timed out"));
    done.clear(); fetcher.start(web.url()); fetcher.cancel(); QCOMPARE(done.size(), 1); QVERIFY(error(done).contains("cancelled"));
    fetcher.cancel(); QCOMPARE(done.size(), 1);
    auto* temporary = new TextPreviewFetcher; temporary->start(web.url()); delete temporary;
    QTest::qWait(75); QCOMPARE(done.size(), 1);
}
void PreviewTests::http_and_authentication_failures()
{
    LocalWeb web;
    web.respond = [](auto* socket, const auto&) {
        socket->write("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=private\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost();
    };
    TextPreviewFetcher fetcher; QSignalSpy done(&fetcher, &TextPreviewFetcher::finished);
    fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QVERIFY(!error(done).isEmpty()); QCOMPARE(web.requests.size(), 1);
    web.respond = [](auto* socket, const auto&) {
        socket->write("HTTP/1.1 404 Missing\r\nContent-Type: text/html\r\nContent-Length: 16\r\nConnection: close\r\n\r\n<p>not found</p>"); socket->disconnectFromHost();
    };
    done.clear(); fetcher.start(web.url()); QTRY_COMPARE(done.size(), 1); QVERIFY(!error(done).isEmpty()); QVERIFY(value(done).empty());
}
void PreviewTests::https_uses_certificate_and_hostname_verification()
{
    QVERIFY(QSslSocket::supportsSsl());
    qInfo() << "TLS backend:" << QSslSocket::activeBackend() << QSslSocket::sslLibraryVersionString();
    QFile certificate(QString(NEBULA_TEST_FIXTURES) + "/preview-tls/test-cert.pem");
    QFile key(QString(NEBULA_TEST_FIXTURES) + "/preview-tls/test-key.pem");
    QVERIFY(certificate.open(QIODevice::ReadOnly)); QVERIFY(key.open(QIODevice::ReadOnly));
    class TlsWeb final : public QTcpServer {
    public:
        QSslCertificate certificate;
        QSslKey key;
        int requests = 0;
        void incomingConnection(qintptr descriptor) override
        {
            auto* socket = new QSslSocket(this);
            socket->setLocalCertificate(certificate); socket->setPrivateKey(key);
            if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
            connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QSslSocket::readyRead, this, [this, socket] {
                socket->readAll(); ++requests;
                LocalWeb::reply(socket, "verified HTTPS text", "text/plain");
            });
            socket->startServerEncryption();
        }
    } web;
    web.certificate = QSslCertificate(certificate.readAll()); web.key = QSslKey(key.readAll(), QSsl::Rsa);
    QVERIFY(!web.certificate.isNull()); QVERIFY(!web.key.isNull()); QVERIFY(web.listen(QHostAddress::LocalHost));
    const auto url = QString("https://127.0.0.1:%1/").arg(web.serverPort());
    TextPreviewFetcher rejected; QSignalSpy refused(&rejected, &TextPreviewFetcher::finished);
    rejected.start(url); QTRY_COMPARE(refused.size(), 1); QVERIFY(!error(refused).isEmpty()); QCOMPARE(web.requests, 0);

    struct RestoreTrust {
        QSslConfiguration original = QSslConfiguration::defaultConfiguration();
        ~RestoreTrust() { QSslConfiguration::setDefaultConfiguration(original); }
    } trust;
    auto configuration = trust.original;
    auto certificates = configuration.caCertificates(); certificates.append(web.certificate);
    configuration.setCaCertificates(certificates); QSslConfiguration::setDefaultConfiguration(configuration);
    TextPreviewFetcher trusted; QSignalSpy accepted(&trusted, &TextPreviewFetcher::finished);
    trusted.start(url); QTRY_COMPARE(accepted.size(), 1);
    QVERIFY2(error(accepted).isEmpty(), qPrintable(error(accepted)));
    QCOMPARE(value(accepted).excerpt, QString("verified HTTPS text")); QCOMPARE(web.requests, 1);
    TextPreviewFetcher mismatch; QSignalSpy wrong_host(&mismatch, &TextPreviewFetcher::finished);
    mismatch.start(QString("https://localhost:%1/").arg(web.serverPort())); QTRY_COMPARE(wrong_host.size(), 1);
    QVERIFY(!error(wrong_host).isEmpty()); QCOMPARE(web.requests, 1);
}
QTEST_MAIN(PreviewTests)
#include "preview_tests.moc"
