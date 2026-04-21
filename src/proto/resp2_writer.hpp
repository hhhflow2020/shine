#pragma once

#include "core/common.hpp"

#include <absl/strings/string_view.h>
#include <absl/types/span.h>

#include <string>
#include <vector>

namespace shine::proto {

// Low-level RESP2 emitters. Write into a std::string buffer.
class Resp2Writer {
public:
    // Emit an Array of BulkStrings.
    static void writeArray(std::string& out, absl::Span<const absl::string_view> parts);
    static void writeArray(std::string& out, const std::vector<std::string>& parts);

    // Emit a single BulkString header "$N\r\n"; caller appends payload + "\r\n"
    // to the writable buffer of the socket. This avoids copying large payloads.
    static void writeBulkHeader(std::string& out, std::size_t payload_len);

    // Emit a complete BulkString (small payloads).
    static void writeBulk(std::string& out, absl::string_view payload);
    static void writeBulk(std::string& out, const u8* data, std::size_t len);
};

} // namespace shine::proto
