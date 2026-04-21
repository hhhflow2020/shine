#include "proto/resp2_writer.hpp"

#include <absl/strings/str_cat.h>

namespace shine::proto {

void Resp2Writer::writeBulkHeader(std::string& out, std::size_t payload_len) {
    absl::StrAppend(&out, "$", payload_len, "\r\n");
}

void Resp2Writer::writeBulk(std::string& out, absl::string_view payload) {
    writeBulkHeader(out, payload.size());
    out.append(payload.data(), payload.size());
    out.append("\r\n", 2);
}

void Resp2Writer::writeBulk(std::string& out, const u8* data, std::size_t len) {
    writeBulkHeader(out, len);
    out.append(reinterpret_cast<const char*>(data), len);
    out.append("\r\n", 2);
}

void Resp2Writer::writeArray(std::string& out, absl::Span<const absl::string_view> parts) {
    absl::StrAppend(&out, "*", parts.size(), "\r\n");
    for (auto p : parts) writeBulk(out, p);
}

void Resp2Writer::writeArray(std::string& out, const std::vector<std::string>& parts) {
    absl::StrAppend(&out, "*", parts.size(), "\r\n");
    for (const auto& p : parts) writeBulk(out, p);
}

} // namespace shine::proto
