#include "nebula/attachments/manifest.hpp"
#include "nebula/attachments/files.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

namespace nebula::files {
namespace {
QString utf8(const std::string& text) {
    const auto value=QString::fromUtf8(text.data(),qsizetype(text.size()));
    if (value.toUtf8().toStdString()!=text) throw std::runtime_error("Backup file names must be valid UTF-8");
    return value;
}
std::set<std::string> data_files(const std::filesystem::path& root) {
    std::set<std::string> result{"nebula.sqlite3"};
    if (std::filesystem::is_symlink(root/"attachments")) throw std::runtime_error("Unsafe backup attachment directory");
    for (const auto& entry:std::filesystem::recursive_directory_iterator(root/"attachments")) {
        if (entry.is_symlink() || (!entry.is_directory() && !entry.is_regular_file())) throw std::runtime_error("Unsafe backup attachment file");
        if (entry.is_regular_file()) result.insert(entry.path().lexically_relative(root).generic_u8string());
    }
    return result;
}
bool safe_relative(const std::string& text) {
    if (text.empty() || text.find('\0')!=std::string::npos) return false;
    const auto path=std::filesystem::u8path(text); if (path.is_absolute()) return false;
    for (const auto& part:path) if (part=="." || part=="..") return false;
    return text=="nebula.sqlite3" || text.rfind("attachments/",0)==0;
}
} // namespace
void write_backup_manifest(const std::filesystem::path& root,const std::string& workspace)
{
    QJsonArray files;
    for (const auto& path:data_files(root)) {
        const auto actual=digest(root/std::filesystem::u8path(path));
        files.append(QJsonObject{{"path",utf8(path)},{"size",QString::number(actual.size)},{"sha256",utf8(actual.sha256)}});
    }
    const auto text=QJsonDocument(QJsonObject{{"version",1},{"workspace_id",utf8(workspace)},{"files",files}}).toJson(QJsonDocument::Indented);
    write_new(root/"backup-manifest.json",text.toStdString()); sync_directory(root);
}
void verify_backup_manifest(const std::filesystem::path& root,const std::string& workspace,bool required)
{
    const auto path=root/"backup-manifest.json";
    if (!std::filesystem::exists(path)) { if (required) throw std::runtime_error("This backup is missing its attachment manifest"); return; }
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) throw std::runtime_error("Unsafe backup manifest");
    QFile file(QString::fromStdString(path.u8string()));
    if (!file.open(QIODevice::ReadOnly) || file.size()>64*1024*1024) throw std::runtime_error("Cannot read backup manifest or manifest exceeds 64 MiB");
    QJsonParseError error; const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid backup manifest JSON");
    const auto object=document.object();
    if (object["version"].toDouble(-1)!=1 || !object["workspace_id"].isString()
        || object["workspace_id"].toString()!=utf8(workspace) || !object["files"].isArray()) throw std::runtime_error("Unsupported or mismatched backup manifest");
    const auto expected=data_files(root);
    std::set<std::string> listed;
    for (const auto& value:object["files"].toArray()) {
        if (!value.isObject()) throw std::runtime_error("Invalid backup manifest file entry");
        const auto entry=value.toObject();
        if (!entry["path"].isString() || !entry["size"].isString() || !entry["sha256"].isString()) throw std::runtime_error("Invalid backup manifest field types");
        const auto relative=entry["path"].toString().toUtf8().toStdString(); bool valid=false;
        const auto size=entry["size"].toString().toLongLong(&valid); const auto checksum=entry["sha256"].toString().toStdString();
        if (!safe_relative(relative) || !listed.insert(relative).second || !valid || size<0 || checksum.size()!=64
            || !std::all_of(checksum.begin(),checksum.end(),[](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');}))
            throw std::runtime_error("Invalid backup manifest path, size or checksum");
        const auto actual=digest(root/std::filesystem::u8path(relative));
        if (actual.size!=size || actual.sha256!=checksum) throw std::runtime_error("Backup file verification failed: "+relative);
    }
    if (listed!=expected) throw std::runtime_error("Backup file list differs from its manifest");
}
} // namespace nebula::files
