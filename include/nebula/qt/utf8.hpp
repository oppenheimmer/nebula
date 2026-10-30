#pragma once

#include <QString>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nebula::utf8 {
// Always pass lengths: embedded NULs are valid text, not terminators. Display
// adapters may replace invalid input; canonical format readers use checked().
inline QString decode(std::string_view bytes)
{
    if (bytes.size() > std::size_t(std::numeric_limits<qsizetype>::max()))
        throw std::length_error("Text exceeds the Qt string size limit");
    return QString::fromUtf8(bytes.data(), qsizetype(bytes.size()));
}
inline std::string encode(const QString& text)
{
    return text.toUtf8().toStdString();
}
template <class Error = std::invalid_argument>
QString checked(std::string_view bytes, const char* message = "Text must be valid UTF-8")
{
    auto text = decode(bytes);
    if (encode(text) != bytes)
        throw Error(message);
    return text;
}
} // namespace nebula::utf8
