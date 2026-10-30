#include "internal.hpp"
#include "nebula/interchange/interchange.hpp"
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <map>
#include <set>

namespace nebula::interchange {
namespace {
using namespace detail;
QString encoded(const std::string& value)
{
    const auto text = q(value);
    static const QRegularExpression safe("^[A-Za-z0-9_.-]+$");
    return safe.match(text).hasMatch() ? text : "~" + QString::fromLatin1(text.toUtf8().toHex());
}
QString filename(const std::string& id)
{
    return "id-" + encoded(id) + ".md";
}
QString escaped(QString value)
{
    QString result;
    for (auto c : value) {
        if (QString("\\*_[]()#!<>+-").contains(c) || c == QChar(0x60))
            result += '\\';
        if (c == QChar(0x2028))
            result += "<br>";
        else if (c == QChar::Null)
            result += QChar::ReplacementCharacter;
        else
            result += c;
    }
    return result;
}
QString styled(const Run& run, QString label)
{
    label = escaped(label);
    if (run.italic)
        label = "*" + label + "*";
    if (run.bold)
        label = "**" + label + "**";
    return label;
}
struct ImportedPage {
    QString file, title;
    std::string id;
    std::vector<Block> blocks;
};
struct InlineContext {
    sqlite3* db;
    const fs::path& source;
    const fs::path& target;
    InterchangeReport& report;
    const std::map<QString, std::string>& pages;
    const std::map<QString, std::string>& anchors;
    std::map<QString, AttachmentInfo>& assets;
    std::vector<fs::path>& copied;
    std::optional<Asset> asset(const QString& url, const QString& label, bool image)
    {
        if (!url.startsWith("attachments/"))
            return {};
        // Markdown writers percent-encode spaces and punctuation; decode before
        // the traversal checks so "%2F" or "%2E%2E" cannot escape the folder.
        const auto name = QUrl::fromPercentEncoding(url.mid(12).toUtf8());
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\') ||
            name.contains(QChar::Null))
            throw std::runtime_error("Unsafe Markdown attachment path");
        if (fs::is_symlink(source / "attachments"))
            throw std::runtime_error("Markdown attachment directory is a symlink");
        const auto visible = label.trimmed().isEmpty() ? name : label;
        auto found = assets.find(name);
        if (found == assets.end()) {
            const auto id = sql::id(db);
            const auto output = target / "attachments" / id;
            files::Digest digest;
            try {
                digest = files::copy_beneath(source, fs::u8path("attachments/" + s(name)), output);
            } catch (const std::invalid_argument&) {
                throw;
            } catch (const std::exception&) {
                warn(report, "Missing or unreadable attachment files keep their link label as text: " + s(name));
                return Asset{image, {}, {}};
            }
            copied.push_back(output);
            AttachmentInfo info{id, s(visible), digest.sha256, digest.size, image};
            sql::Statement add(db, "INSERT INTO attachments(id,name,size,sha256,image) VALUES(?,?,?,?,?)");
            add.bind(1, id).bind(2, info.name).bind(3, info.size).bind(4, info.sha256).bind(5, image).done();
            found = assets.emplace(name, std::move(info)).first;
        }
        return Asset{image, s(visible), found->second.id};
    }
};
std::vector<Run> inline_text(const QString& input, InlineContext* context, InterchangeReport& report, bool links = true)
{
    std::vector<Run> runs;
    QString plain;
    bool bold = false, italic = false;
    auto flush = [&] {
        if (!plain.isEmpty()) {
            runs.push_back({s(plain), bold, italic});
            plain.clear();
        }
    };
    auto closing = [&](qsizetype start, QChar c) {
        for (auto i = start; i < input.size(); ++i) {
            if (input[i] == '\\')
                ++i;
            else if (input[i] == c)
                return i;
        }
        return qsizetype(-1);
    };
    for (qsizetype i = 0; i < input.size();) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            plain += input[i + 1];
            i += 2;
            continue;
        }
        if (input.mid(i, 4) == "<br>") {
            plain += QChar(0x2028);
            i += 4;
            continue;
        }
        if (input.mid(i, 2) == "**") {
            flush();
            bold = !bold;
            i += 2;
            continue;
        }
        if (input[i] == '*') {
            flush();
            italic = !italic;
            ++i;
            continue;
        }
        const bool image = input.mid(i, 2) == "![";
        if (links && context && (image || input[i] == '[')) {
            const auto first = i + (image ? 2 : 1), end = closing(first, ']');
            const auto finish = end >= 0 && input.mid(end, 2) == "](" ? closing(end + 2, ')') : -1;
            if (finish >= 0) {
                const auto label = input.mid(first, end - first), url = input.mid(end + 2, finish - end - 2);
                flush();
                auto labels = inline_text(label, nullptr, report, false);
                QString visible;
                for (const auto& run : labels)
                    visible += q(run.text);
                const auto asset = context->asset(url, visible, image);
                if (asset && !asset->attachment_id.empty())
                    runs.push_back({{}, bold, italic, {}, *asset});
                else if (asset) {
                    for (auto& run : labels) {
                        run.bold = run.bold || bold;
                        run.italic = run.italic || italic;
                        runs.push_back(std::move(run));
                    }
                } else {
                    const auto hash = url.indexOf('#');
                    const auto file = hash < 0 ? url : url.left(hash);
                    const auto page = context->pages.find(file);
                    std::optional<Reference> reference;
                    if (!image && page != context->pages.end()) {
                        if (hash < 0)
                            reference = Reference{ReferenceKind::Page, page->second};
                        else if (const auto block = context->anchors.find(url); block != context->anchors.end())
                            reference = Reference{ReferenceKind::Block, block->second};
                        else
                            reference = Reference{ReferenceKind::Block, {}, s(url)};
                    } else if (!image && (url.startsWith("id-") || (url.endsWith(".md") && !url.contains(':'))))
                        reference = Reference{ReferenceKind::Page, {}, s(url)};
                    else
                        warn(report, "External links, remote images and unsupported link destinations become label "
                                     "text; no resources are fetched.");
                    for (auto& run : labels) {
                        run.bold = run.bold || bold;
                        run.italic = run.italic || italic;
                        run.reference = reference;
                        runs.push_back(std::move(run));
                    }
                    if (reference && reference->target_id.empty())
                        warn(report, "Missing page/block destinations remain explicit unresolved references.");
                }
                i = finish + 1;
                continue;
            }
        }
        if (input[i] == '<' || input[i] == '_' || input[i] == QChar(0x60))
            warn(report, "Unsupported HTML, underscore emphasis, code, tables and other Markdown constructs remain "
                         "literal text.");
        plain += input[i++];
    }
    flush();
    if (bold || italic)
        warn(report, "Unbalanced asterisk emphasis is interpreted through the end of its paragraph.");
    return runs;
}
QString plain_label(const std::vector<Run>& runs)
{
    QString text;
    for (const auto& run : runs)
        text += q(run.text);
    return text;
}
} // namespace

InterchangeReport export_markdown(const Workspace& source, const std::filesystem::path& destination)
{
    const auto& directory_ = source.directory();
    reject_managed_destination(directory_, destination);
    files::WorkspaceLock lock(directory_, false);
    NewDirectory output(destination);
    ExportSnapshot snapshot(source, destination);
    const auto& workspace = snapshot.workspace();
    InterchangeReport report;
    warn(report, "Markdown omits database membership, properties, views, timestamps, revisions, trash, recovery "
                 "history and stable identities. Use lossless JSON for preservation.");
    std::map<std::string, std::string> owners;
    std::vector<PageRecord> all;
    for (const auto& db : workspace.databases())
        for (auto page : workspace.pages(db.id)) {
            for (const auto& block : page.document.blocks)
                owners[block.id] = page.document.id;
            all.push_back(std::move(page));
        }
    std::set<std::string> included;
    for (const auto& page : all)
        included.insert(page.document.id);
    std::set<std::string> copied;
    auto asset = [&](bool image, const std::string& label, const std::string& id) {
        if (id.empty()) {
            warn(report, "Legacy label-only image/file cards export as text because no managed bytes exist.");
            return escaped(q(label));
        }
        const auto info = workspace.attachment(id);
        if (!info)
            throw std::runtime_error("Missing managed attachment metadata: " + id);
        if (copied.insert(id).second) {
            files::make_private_directory(destination / "attachments");
            workspace.attachment_path(id);
            const auto digest =
                files::copy_beneath(directory_, fs::u8path("attachments/" + id), destination / "attachments" / id);
            if (digest.sha256 != info->sha256 || digest.size != info->size)
                throw std::runtime_error("Managed attachment checksum mismatch");
        }
        return (image ? "![" : "[") + escaped(q(label)) + "](attachments/" + q(id) + ")";
    };
    for (const auto& page : all) {
        auto title = q(page.title);
        if (title.contains('\n') || title.contains('\r') || title.contains(QChar::Null))
            warn(report,
                 "Title line breaks become spaces and embedded NUL becomes a replacement character in Markdown.");
        title.replace('\n', ' ');
        title.replace('\r', ' ');
        QString markdown = "# " + escaped(title) + "\n\n";
        for (const auto& block : page.document.blocks) {
            if (!block.parent_id.empty())
                warn(report,
                     "Block nesting is flattened in Markdown; lists and to-dos retain their individual block kinds.");
            markdown += "<a id=\"block-" + encoded(block.id) + "\"></a>\n";
            QString body;
            if (block.kind == BlockKind::Image || block.kind == BlockKind::Attachment)
                body = asset(block.kind == BlockKind::Image, block.asset_label, block.attachment_id);
            else
                for (const auto& run : block.runs) {
                    if (run.asset) {
                        if (run.bold || run.italic)
                            warn(report, "Inline asset bold/italic presentation is omitted in Markdown.");
                        body += asset(run.asset->image, run.asset->label, run.asset->attachment_id);
                        continue;
                    }
                    const auto label = run.reference && !run.reference->custom_label
                                           ? workspace.reference_label(*run.reference).value_or(run.text)
                                           : run.text;
                    if (label.find('\0') != std::string::npos)
                        warn(report, "Embedded NUL text becomes the Unicode replacement character in Markdown.");
                    auto value = styled(run, q(label));
                    if (run.reference) {
                        std::string owner = run.reference->kind == ReferenceKind::Page
                                                ? run.reference->target_id
                                                : owners[run.reference->target_id];
                        if (!owner.empty() && included.count(owner)) {
                            QString url = filename(owner);
                            if (run.reference->kind == ReferenceKind::Block)
                                url += "#block-" + encoded(run.reference->target_id);
                            value = "[" + value + "](" + url + ")";
                            if (!run.reference->custom_label)
                                warn(report, "Markdown references import with fixed custom labels; default-label mode "
                                             "is not preserved.");
                        } else
                            warn(report,
                                 "Unresolved, trashed, unavailable or out-of-set targets export as label text.");
                    }
                    body += value;
                }
            switch (block.kind) {
            case BlockKind::Heading:
                body = "## " + body;
                break;
            case BlockKind::Bullet:
                body = "- " + body;
                break;
            case BlockKind::Todo:
                body = QString(block.checked ? "- [x] " : "- [ ] ") + body;
                break;
            case BlockKind::Divider:
                body = "---";
                break;
            default:
                break;
            }
            if (body.isEmpty())
                body = "<!-- nebula-empty -->";
            markdown += body + "\n\n";
        }
        files::write_new(destination / fs::u8path(s(filename(page.document.id))), s(markdown));
        report.pages.push_back(page.document.id);
    }
    std::string warnings = "Nebula Markdown subset. Every .md file imports as a new page.\n";
    for (const auto& message : report.warnings)
        warnings += message + "\n";
    files::write_new(destination / "CONVERSION.txt", warnings);
    if (fs::exists(destination / "attachments"))
        files::sync_directory(destination / "attachments");
    snapshot.close();
    output.complete();
    return report;
}

InterchangeReport import_markdown(Workspace& workspace, const std::filesystem::path& source,
                                  const std::string& database)
{
    const auto& directory_ = workspace.directory();
    sqlite3* connection = WorkspaceInternals::connection(workspace);
    if (!source.is_absolute() || fs::is_symlink(source) || !fs::is_directory(source))
        throw std::invalid_argument("Choose an existing regular Markdown folder");
    std::vector<fs::path> inputs;
    InterchangeReport report;
    for (const auto& entry : fs::directory_iterator(source)) {
        if (entry.path().extension() == ".md") {
            if (entry.is_symlink() || !entry.is_regular_file())
                throw std::runtime_error("Markdown inputs must be regular files");
            inputs.push_back(entry.path());
        } else if (entry.path().filename() != "attachments" && entry.path().filename() != "CONVERSION.txt")
            warn(report, "Non-Markdown entries are ignored; only top-level .md files are imported.");
    }
    std::sort(inputs.begin(), inputs.end());
    if (inputs.empty())
        throw std::invalid_argument("No top-level .md files found");
    files::WorkspaceLock lock(directory_, true);
    sql::Transaction transaction(connection);
    sql::Statement active(connection, "SELECT id FROM databases WHERE id=? AND NOT trashed");
    active.bind(1, database);
    if (!active.row())
        throw std::invalid_argument("Choose an active destination database");
    std::vector<ImportedPage> imported;
    std::map<QString, std::string> page_ids, anchors;
    std::vector<fs::path> copied;
    std::map<QString, AttachmentInfo> assets;
    try {
        // Allocate every identity before resolving links, including forward links.
        for (const auto& input : inputs) {
            ImportedPage page;
            page.file = q(input.filename().u8string());
            page.id = sql::id(connection);
            page_ids[page.file] = page.id;
            auto content = q(read(input));
            if (content.contains(QChar::Null))
                throw std::runtime_error("Markdown cannot contain NUL characters");
            content.replace("\r\n", "\n");
            content.replace('\r', '\n');
            if (content.startsWith(QChar(0xfeff)))
                content.remove(0, 1);
            auto lines = content.split('\n');
            page.title = q(input.stem().u8string());
            qsizetype start = 0;
            if (!lines.isEmpty() && lines[0].startsWith("# ")) {
                page.title = plain_label(inline_text(lines[0].mid(2), nullptr, report));
                start = 1;
            }
            QString paragraph, anchor;
            BlockKind kind = BlockKind::Paragraph;
            bool checked = false;
            auto flush = [&] {
                if (paragraph.isEmpty() && anchor.isEmpty())
                    return;
                Block block{sql::id(connection), {}, kind, {{s(paragraph)}}};
                block.checked = checked;
                if (!anchor.isEmpty()) {
                    if (!anchors.emplace(page.file + "#" + anchor, block.id).second)
                        throw std::runtime_error("Duplicate Markdown block anchor");
                }
                page.blocks.push_back(std::move(block));
                paragraph.clear();
                anchor.clear();
                kind = BlockKind::Paragraph;
                checked = false;
            };
            const QRegularExpression marker("^<a id=\"(block-[A-Za-z0-9_~%.-]+)\"></a>$");
            for (auto i = start; i < lines.size(); ++i) {
                const auto line = lines[i];
                const auto match = marker.match(line);
                if (match.hasMatch()) {
                    flush();
                    anchor = match.captured(1);
                    continue;
                }
                if (line.trimmed().isEmpty()) {
                    flush();
                    continue;
                }
                const bool structural = line.startsWith("## ") || line.startsWith("- ") || line == "---";
                if (!paragraph.isEmpty() && structural)
                    flush();
                if (!paragraph.isEmpty()) {
                    paragraph += QChar(0x2028);
                    paragraph += line;
                    continue;
                }
                QString body = line;
                if (body.startsWith("## ")) {
                    kind = BlockKind::Heading;
                    body.remove(0, 3);
                } else if (body.startsWith("- [ ] ") || body.startsWith("- [x] ") || body.startsWith("- [X] ")) {
                    kind = BlockKind::Todo;
                    checked = body[3] != ' ';
                    body.remove(0, 6);
                } else if (body.startsWith("- ")) {
                    kind = BlockKind::Bullet;
                    body.remove(0, 2);
                } else if (body == "---") {
                    kind = BlockKind::Divider;
                    body.clear();
                } else if (body == "<!-- nebula-empty -->")
                    body.clear();
                else if (body.startsWith('#') || body.startsWith('>') || body.startsWith("  ") || body.contains('|'))
                    warn(report,
                         "Unsupported heading levels, quotes, nesting and tables remain literal paragraph text.");
                paragraph = body;
                if (kind == BlockKind::Divider || body.isEmpty()) {
                    if (anchor.isEmpty())
                        anchor = "generated-" + q(sql::id(connection));
                    flush();
                }
            }
            flush();
            if (page.blocks.empty())
                page.blocks.push_back({sql::id(connection)});
            imported.push_back(std::move(page));
        }
        InlineContext context{connection, source, directory_, report, page_ids, anchors, assets, copied};
        for (auto& page : imported) {
            for (auto& block : page.blocks) {
                const auto content = block.runs.empty() ? QString{} : q(block.runs[0].text);
                block.runs = inline_text(content, &context, report);
                if (block.kind == BlockKind::Paragraph && block.runs.size() == 1 && block.runs[0].asset) {
                    const auto asset = *block.runs[0].asset;
                    block.kind = asset.image ? BlockKind::Image : BlockKind::Attachment;
                    block.asset_label = asset.label;
                    block.attachment_id = asset.attachment_id;
                    block.runs.clear();
                }
            }
            WorkspaceInternals::create_page_in_transaction(workspace, database, s(page.title), page.blocks, page.id);
            report.pages.push_back(page.id);
        }
        // Each page was indexed as it was created; imported links use fixed labels,
        // so no workspace-wide rebuild is needed.
        files::sync_directory(directory_ / "attachments");
        transaction.commit();
    } catch (...) {
        for (const auto& file : copied) {
            std::error_code error;
            fs::remove(file, error);
        }
        throw;
    }
    warn(report, "Imported pages and blocks receive new identities; links within the imported set are remapped. "
                 "Existing pages are never overwritten.");
    return report;
}
} // namespace nebula::interchange
