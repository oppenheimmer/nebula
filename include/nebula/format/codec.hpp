#pragma once
#include "nebula/core/database.hpp"

namespace nebula {
// Application-owned block JSON v2 (reads v1); other envelopes remain v1.
// Public interfaces contain no Qt types.
std::string encode_block(const Block& block);
void decode_block(Block& block, const std::string& json);
std::string encode_value(const PropertyValue& value);
PropertyValue decode_value(const std::string& json);
std::string encode_view(const View& view);
void decode_view(View& view, const std::string& json);
std::string encode_document(const Page& page);
Page decode_document(const std::string& json);
} // namespace nebula
