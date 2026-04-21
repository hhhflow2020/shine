#pragma once

#include "core/buffer.hpp"
#include "core/common.hpp"

#include <absl/strings/string_view.h>
#include <absl/types/variant.h>

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace shine::proto {

// A decoded RESP2 top-level value. We only support the subset we need:
//   * top-level BulkString   (DATA frame, binary payload)
//   * top-level Array of BulkStrings (control frame)
// Anything else is a decode error.
struct BulkStringView {
    const u8*   data;
    std::size_t size;
};

struct Resp2BulkFrame {
    BulkStringView bulk;  // pointers into the parser's buffer, valid until consume()
};

struct Resp2ArrayFrame {
    // Each element is a bulk string (shared_ptr owning the decoded bytes -- we
    // copy small arrays to decouple lifetime from the read buffer).
    std::vector<std::string> elements;
};

using Resp2Frame = std::variant<std::monostate, Resp2BulkFrame, Resp2ArrayFrame>;

class Resp2Parser {
public:
    // 512 MB like Redis.
    static constexpr std::size_t kMaxBulkLen = 512ull * 1024 * 1024;

    explicit Resp2Parser(std::size_t max_bulk = kMaxBulkLen);

    // Try to parse one complete top-level value from buf. On success, writes
    // into `out` and returns Ok(bytes_consumed). If incomplete, returns
    // Ok(0) and leaves buf untouched. On protocol violation returns error.
    StatusOr<std::size_t> tryParse(const ReadBuffer& buf, Resp2Frame& out);

private:
    std::size_t max_bulk_;
};

} // namespace shine::proto
