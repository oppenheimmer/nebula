#include "nebula/format/codec.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace nebula {
namespace {
QString q(const std::string& s) {
    const auto result = QString::fromUtf8(s.data(), qsizetype(s.size()));
    if (result.toUtf8().toStdString() != s) throw std::invalid_argument("Text must be valid UTF-8");
    return result;
}
std::string s(const QJsonValue& value) {
    if (!value.isString()) throw std::runtime_error("Expected JSON text");
    return value.toString().toUtf8().toStdString();
}
bool flag(const QJsonValue& value) {
    if (!value.isBool()) throw std::runtime_error("Expected JSON boolean");
    return value.toBool();
}
int integer(const QJsonValue& value, int max) {
    const auto n = value.toInt(-1);
    if (!value.isDouble() || n < 0 || n > max || value.toDouble() != n)
        throw std::runtime_error("Invalid JSON enum/version");
    return n;
}
QJsonArray array(const QJsonValue& value) {
    if (!value.isArray()) throw std::runtime_error("Expected JSON array");
    return value.toArray();
}
QJsonObject object(const QJsonValue& value) {
    if (!value.isObject()) throw std::runtime_error("Expected JSON object");
    return value.toObject();
}
QJsonObject parse(const std::string& text, int maximum_version=1) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid payload JSON");
    auto obj = doc.object();
    if (integer(obj["version"], maximum_version)<1) throw std::runtime_error("Unsupported payload version");
    return obj;
}
std::string serialize(const QJsonObject& obj) { return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString(); }
QJsonObject value_object(const PropertyValue& value) {
    if (value.index()) validate_value(PropertyKind(value.index()-1),value);
    QJsonObject out{{"version", 1}, {"type", int(value.index())}};
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) out["value"] = q(v);
        else if constexpr (std::is_same_v<T, Date>) out["value"] = q(v.iso);
        else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            QJsonArray tags; for (const auto& tag : v) tags.append(q(tag)); out["value"] = tags;
        } else if constexpr (!std::is_same_v<T, std::monostate>) out["value"] = v;
    }, value);
    return out;
}
PropertyValue read_value(const QJsonObject& obj) {
    if (integer(obj["version"],1)!=1) throw std::runtime_error("Unsupported property payload version");
    const auto type = integer(obj["type"], 5);
    const auto v = obj["value"];
    PropertyValue result;
    switch (type) {
    case 0: return result;
    case 1: result = s(v); break;
    case 2: if (!v.isDouble()) throw std::runtime_error("Expected number"); result = v.toDouble(); break;
    case 3: result = Date{s(v)}; break;
    case 4: result = flag(v); break;
    case 5: {
        std::vector<std::string> tags;
        for (const auto& item : array(v)) tags.push_back(s(item));
        result = std::move(tags); break;
    }
    }
    validate_value(static_cast<PropertyKind>(type - 1), result);
    return result;
}
} // namespace
std::string encode_block(const Block& block)
{
    QJsonArray runs;
    for (const auto& run : block.runs) {
        QJsonObject out{{"text", q(run.text)}, {"bold", run.bold}, {"italic", run.italic}};
        if (run.reference) out["reference"] = QJsonObject{{"kind", int(run.reference->kind)}, {"target", q(run.reference->target_id)},
            {"unresolved_name",q(run.reference->unresolved_name)},{"scope",q(run.reference->database_scope)},
            {"custom_label",run.reference->custom_label}};
        if (run.asset) out["asset"] = QJsonObject{{"image", run.asset->image}, {"label", q(run.asset->label)}, {"attachment_id",q(run.asset->attachment_id)}};
        runs.append(out);
    }
    return serialize({{"version", 3}, {"runs", runs}, {"asset_label", q(block.asset_label)}, {"checked", block.checked}, {"attachment_id",q(block.attachment_id)}});
}
void decode_block(Block& block, const std::string& json)
{
    const auto obj = parse(json,3);
    block.runs.clear(); block.asset_label = s(obj["asset_label"]); block.checked = flag(obj["checked"]);
    block.attachment_id=obj["version"].toInt()>=3?s(obj["attachment_id"]):std::string{};
    for (const auto& item : array(obj["runs"])) {
        const auto run = object(item);
        Run out{s(run["text"]), flag(run["bold"]), flag(run["italic"])};
        if (run.contains("reference")) {
            const auto ref = object(run["reference"]);
            out.reference = Reference{static_cast<ReferenceKind>(integer(ref["kind"], 1)), s(ref["target"])};
            if (ref.contains("unresolved_name")) out.reference->unresolved_name=s(ref["unresolved_name"]);
            if (ref.contains("scope")) out.reference->database_scope=s(ref["scope"]);
            if (obj["version"].toInt()>=2) out.reference->custom_label=flag(ref["custom_label"]);
        }
        if (run.contains("asset")) {
            const auto asset = object(run["asset"]);
            out.asset = Asset{flag(asset["image"]), s(asset["label"]),obj["version"].toInt()>=3?s(asset["attachment_id"]):std::string{}};
        }
        block.runs.push_back(std::move(out));
    }
}
std::string encode_value(const PropertyValue& value) { return serialize(value_object(value)); }
PropertyValue decode_value(const std::string& json) { return read_value(parse(json)); }
std::string encode_view(const View& view)
{
    QJsonArray visible, filters;
    for (const auto& id : view.visible_properties) visible.append(q(id));
    for (const auto& filter : view.filters) filters.append(QJsonObject{{"property", q(filter.property_id)},
        {"comparison", int(filter.comparison)}, {"value", value_object(filter.value)}});
    return serialize({{"version", 1}, {"visible", visible}, {"filters", filters},
        {"sort", q(view.sort_property)}, {"ascending", view.ascending}});
}
void decode_view(View& view, const std::string& json)
{
    const auto obj = parse(json);
    view.visible_properties.clear(); view.filters.clear();
    for (const auto& id : array(obj["visible"])) view.visible_properties.push_back(s(id));
    for (const auto& item : array(obj["filters"])) {
        const auto filter = object(item);
        view.filters.push_back({s(filter["property"]), static_cast<Comparison>(integer(filter["comparison"], 5)),
            read_value(object(filter["value"]))});
    }
    view.sort_property = s(obj["sort"]); view.ascending = flag(obj["ascending"]);
}
std::string encode_document(const Page& page)
{
    validate(page); QJsonArray blocks;
    for (const auto& block:page.blocks) blocks.append(QJsonObject{{"id",q(block.id)},
        {"parent",q(block.parent_id)},{"kind",int(block.kind)},{"payload",parse(encode_block(block),3)}});
    return serialize({{"version",1},{"id",q(page.id)},{"blocks",blocks}});
}
Page decode_document(const std::string& json)
{
    const auto root=parse(json); Page page{s(root["id"]),{}};
    for (const auto& entry:array(root["blocks"])) {
        const auto obj=object(entry); Block block{s(obj["id"]),s(obj["parent"]),BlockKind(integer(obj["kind"],6))};
        decode_block(block,serialize(object(obj["payload"]))); page.blocks.push_back(std::move(block));
    }
    validate(page); return page;
}
} // namespace nebula
