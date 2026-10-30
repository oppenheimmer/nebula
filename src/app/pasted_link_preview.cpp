#include "nebula/app/pasted_link_preview.hpp"
#include "nebula/editor/page_editor.hpp"
#include "nebula/qt/utf8.hpp"
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputMethodEvent>
#include <QLabel>
#include <QStatusBar>
#include <QTextBlock>
#include <QToolButton>
#include <QUuid>
#include <algorithm>
#include <unordered_set>

namespace nebula {
PastedLinkPreview::PastedLinkPreview(PageEditor* editor, QStatusBar* status, QObject* parent, PreviewLimits limits)
    : QObject(parent), editor_(editor), status_(status), limits_(limits)
{
    setObjectName("pastedLinkPreview");
    editor->installEventFilter(this);
    connect(editor, &QObject::destroyed, this, [this] { finish({}, false); });
    connect(editor, &PageEditor::web_link_pasted, this, &PastedLinkPreview::begin);
    check_.setSingleShot(true);
    connect(editor, &QTextEdit::textChanged, this, [this] {
        if (active_)
            check_.start(0);
    });
    connect(&check_, &QTimer::timeout, this, [this] {
        if (active_ && !valid_bookmark())
            finish("Link preview cancelled because the pasted link changed.");
    });
}
PastedLinkPreview::~PastedLinkPreview()
{
    finish({}, false);
}
bool PastedLinkPreview::valid_bookmark() const
{
    if (!editor_ || !editor_->isVisible() || !editor_->isEnabled() || editor_->isReadOnly() || bookmark_.isNull() ||
        bookmark_.document() != editor_->document())
        return false;
    const auto text = bookmark_.selectedText();
    // QTextCursor follows edits. Space-separated text typed after the URL is
    // harmless; extending or replacing the URL invalidates this result.
    return text.startsWith(url_) && (text.size() == url_.size() || text.at(url_.size()).isSpace());
}
void PastedLinkPreview::begin(const QString& url, const QString& source_block)
{
    finish({}, false);
    if (!editor_ || !editor_->isEnabled() || editor_->isReadOnly() || !preview_url(url))
        return;
    const auto cursor = editor_->textCursor();
    if (cursor.positionInBlock() < url.size() || editor_->current_block_id() != source_block)
        return;
    bookmark_ = cursor;
    bookmark_.setPosition(cursor.position() - url.size());
    bookmark_.setPosition(cursor.position(), QTextCursor::KeepAnchor);
    if (bookmark_.selectedText() != url)
        return;
    try {
        page_ = QString::fromStdString(editor_->snapshot().id);
    } catch (const std::exception& e) {
        finish("Link kept; preview could not start: " + QString::fromUtf8(e.what()));
        return;
    }
    block_ = source_block;
    url_ = url;
    active_ = true;

    if (status_) {
        auto* controls = new QWidget(status_);
        controls->setObjectName("linkPreviewProgress");
        auto* layout = new QHBoxLayout(controls);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel("Fetching link preview…", controls));
        auto* cancel_button = new QToolButton(controls);
        cancel_button->setObjectName("cancelLinkPreview");
        cancel_button->setText("Cancel preview");
        cancel_button->setAccessibleName("Cancel link preview");
        cancel_button->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(cancel_button);
        connect(cancel_button, &QToolButton::clicked, this, &PastedLinkPreview::cancel);
        status_->addWidget(controls);
        controls_ = controls;
    }
    const auto generation = generation_;
    fetcher_ = new TextPreviewFetcher(this, limits_);
    connect(fetcher_, &TextPreviewFetcher::finished, this,
            [this, generation](const TextPreview& preview, const QString& error) {
                if (!active_ || generation != generation_)
                    return;
                fetcher_->deleteLater();
                fetcher_ = nullptr;
                if (!error.isEmpty()) {
                    finish("Link kept; " + error);
                    return;
                }
                ready_ = preview;
                // Commit/cancel the preedit first; rewriting a document during IME
                // composition would discard the user's active input.
                if (!composing_)
                    insert_ready();
            });
    fetcher_->start(url);
}
void PastedLinkPreview::cancel()
{
    if (active_)
        finish("Link kept; preview cancelled.");
}
bool PastedLinkPreview::eventFilter(QObject* watched, QEvent* event)
{
    // This filter is installed only on the editor. Comparing a typed QPointer
    // here downcasts during QWidget destruction, after PageEditor has gone.
    {
        if (event->type() == QEvent::InputMethod) {
            composing_ = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
            if (!composing_ && active_ && ready_)
                QTimer::singleShot(0, this, &PastedLinkPreview::insert_ready);
        } else if (event->type() == QEvent::FocusOut) {
            composing_ = false;
            if (active_ && ready_)
                QTimer::singleShot(0, this, &PastedLinkPreview::insert_ready);
        } else if (event->type() == QEvent::Hide && active_)
            cancel();
    }
    return QObject::eventFilter(watched, event);
}
void PastedLinkPreview::insert_ready()
{
    if (!active_ || !ready_ || composing_)
        return;
    if (QApplication::activeModalWidget()) {
        QTimer::singleShot(100, this, &PastedLinkPreview::insert_ready);
        return;
    }
    if (!valid_bookmark()) {
        finish("Link preview cancelled because the pasted link changed.");
        return;
    }
    try {
        auto page = editor_->snapshot();
        const auto source = std::find_if(page.blocks.begin(), page.blocks.end(), [this](const auto& block) {
            return QString::fromStdString(block.id) == block_;
        });
        if (QString::fromStdString(page.id) != page_ || source == page.blocks.end()) {
            finish("Link preview cancelled because the page changed.");
            return;
        }
        std::unordered_set<std::string> subtree{source->id};
        auto after = source + 1;
        while (after != page.blocks.end() && subtree.count(after->parent_id)) {
            subtree.insert(after->id);
            ++after;
        }
        const auto id = QUuid::createUuid().toString(QUuid::Id128);
        Block preview{id.toStdString(), source->parent_id, BlockKind::Paragraph, {}};
        if (!ready_->title.isEmpty()) {
            Run title;
            title.text = utf8::encode(ready_->title);
            title.bold = true;
            preview.runs.push_back(std::move(title));
        }
        if (!ready_->excerpt.isEmpty() && ready_->excerpt != ready_->title) {
            const auto separator = preview.runs.empty() ? QString{} : QString(QChar::LineSeparator);
            Run excerpt;
            excerpt.text = utf8::encode(separator + ready_->excerpt);
            preview.runs.push_back(std::move(excerpt));
        }
        page.blocks.insert(after, std::move(preview));
        validate(page);
        const auto position = editor_->capture_position();
        finish({}, false);
        editor_->apply_document(page, QString::fromStdString(position.block));
        editor_->restore_position(position);
        if (status_)
            status_->showMessage("Preview added. Remove its block to keep just the link.", 8000);
        emit preview_inserted(id);
        emit preview_finished({});
    } catch (const std::exception& e) {
        finish("Link kept; preview could not be added: " + QString::fromUtf8(e.what()));
    }
}
void PastedLinkPreview::finish(const QString& error, bool report)
{
    ++generation_;
    active_ = false;
    ready_.reset();
    check_.stop();
    bookmark_ = {};
    if (fetcher_) {
        fetcher_->disconnect(this);
        fetcher_->cancel();
        fetcher_->deleteLater();
        fetcher_ = nullptr;
    }
    if (controls_) {
        if (status_)
            status_->removeWidget(controls_);
        controls_->deleteLater();
        controls_ = nullptr;
    }
    if (report) {
        if (status_)
            status_->showMessage(error, 8000);
        emit preview_finished(error);
    }
}
} // namespace nebula
