#include "proto/resp2_parser.hpp"

#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>

#include <cstring>

namespace shine::proto {

namespace {

// Find CRLF starting from offset, or return npos.
std::size_t findCrlf(const u8* data, std::size_t size, std::size_t from) {
    if (size < 2) return std::string::npos;
    for (std::size_t i = from; i + 1 < size; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') return i;
    }
    return std::string::npos;
}

bool parseInt64(const u8* data, std::size_t len, i64& out) {
    absl::string_view sv(reinterpret_cast<const char*>(data), len);
    return absl::SimpleAtoi(sv, &out);
}

// Parse a single BulkString starting at `off`. If incomplete, sets
// incomplete=true. On success, sets value_off/value_len and writes end offset
// into `end_off`.
Status parseBulkString(const u8* data, std::size_t size, std::size_t off,
                      std::size_t max_bulk, bool& incomplete,
                      std::size_t& value_off, std::size_t& value_len,
                      std::size_t& end_off) {
    incomplete = false;
    if (off >= size) { incomplete = true; return absl::OkStatus(); }
    if (data[off] != '$') {
        return absl::InvalidArgumentError("expected '$' bulk marker");
    }
    std::size_t hdr_end = findCrlf(data, size, off + 1);
    if (hdr_end == std::string::npos) { incomplete = true; return absl::OkStatus(); }
    i64 len = 0;
    if (!parseInt64(data + off + 1, hdr_end - off - 1, len)) {
        return absl::InvalidArgumentError("bad bulk length");
    }
    if (len < 0) {
        // null bulk: "$-1\r\n", treat as empty error for our protocol
        return absl::InvalidArgumentError("null bulk not supported");
    }
    if (static_cast<std::size_t>(len) > max_bulk) {
        return absl::ResourceExhaustedError(
            absl::StrCat("bulk length ", len, " exceeds max ", max_bulk));
    }
    std::size_t body_start = hdr_end + 2;
    std::size_t body_end   = body_start + static_cast<std::size_t>(len);
    if (body_end + 2 > size) { incomplete = true; return absl::OkStatus(); }
    if (data[body_end] != '\r' || data[body_end + 1] != '\n') {
        return absl::InvalidArgumentError("missing CRLF after bulk body");
    }
    value_off = body_start;
    value_len = static_cast<std::size_t>(len);
    end_off   = body_end + 2;
    return absl::OkStatus();
}

} // namespace

Resp2Parser::Resp2Parser(std::size_t max_bulk) : max_bulk_(max_bulk) {}

StatusOr<std::size_t> Resp2Parser::tryParse(const ReadBuffer& rb, Resp2Frame& out) {
    const u8*   data = rb.data();
    std::size_t size = rb.size();
    if (size == 0) return 0ULL;

    u8 type = data[0];
    if (type == '$') {
        bool inc = false;
        std::size_t vo = 0, vl = 0, eo = 0;
        auto st = parseBulkString(data, size, 0, max_bulk_, inc, vo, vl, eo);
        if (!st.ok()) return st;
        if (inc) return 0ULL;
        out = Resp2BulkFrame{BulkStringView{data + vo, vl}};
        return eo;
    }
    if (type == '*') {
        std::size_t hdr_end = findCrlf(data, size, 1);
        if (hdr_end == std::string::npos) return 0ULL;
        i64 n = 0;
        if (!parseInt64(data + 1, hdr_end - 1, n)) {
            return absl::InvalidArgumentError("bad array length");
        }
        if (n < 0 || n > 1024) {
            return absl::InvalidArgumentError("array length out of range");
        }
        Resp2ArrayFrame arr;
        arr.elements.reserve(static_cast<std::size_t>(n));
        std::size_t off = hdr_end + 2;
        for (i64 i = 0; i < n; ++i) {
            bool inc = false;
            std::size_t vo = 0, vl = 0, eo = 0;
            auto st = parseBulkString(data, size, off, max_bulk_, inc, vo, vl, eo);
            if (!st.ok()) return st;
            if (inc) return 0ULL;
            arr.elements.emplace_back(reinterpret_cast<const char*>(data + vo), vl);
            off = eo;
        }
        out = std::move(arr);
        return off;
    }
    return absl::InvalidArgumentError(
        absl::StrCat("unexpected RESP2 type byte 0x",
                     absl::Hex(type, absl::kZeroPad2)));
}

} // namespace shine::proto
