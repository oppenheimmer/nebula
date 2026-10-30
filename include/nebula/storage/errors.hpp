#pragma once
#include <stdexcept>
#include <string>

namespace nebula {

// Storage failures carry a category so the desktop can offer the matching
// recovery (reload, retry, correct input) instead of parsing English text.
enum class ErrorKind { Conflict, Busy, Unavailable, Invalid, Io, Corrupt, Unsupported };

class StorageError : public std::runtime_error {
public:
    StorageError(ErrorKind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}
    ErrorKind kind() const noexcept { return kind_; }

private:
    ErrorKind kind_;
};

inline ErrorKind error_kind(const std::exception& error)
{
    if (const auto* storage = dynamic_cast<const StorageError*>(&error))
        return storage->kind();
    if (dynamic_cast<const std::invalid_argument*>(&error))
        return ErrorKind::Invalid;
    return ErrorKind::Io;
}

} // namespace nebula
