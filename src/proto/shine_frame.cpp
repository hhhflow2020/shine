#include "proto/shine_frame.hpp"
#include "proto/resp2_writer.hpp"

#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <cstring>

namespace shine::proto {

namespace {

absl::string_view sv(const std::string& s) {
    return absl::string_view(s.data(), s.size());
}

} // namespace

void putU32BE(std::string& out, u32 v) {
    char b[4];
    b[0] = static_cast<char>((v >> 24) & 0xff);
    b[1] = static_cast<char>((v >> 16) & 0xff);
    b[2] = static_cast<char>((v >> 8)  & 0xff);
    b[3] = static_cast<char>(v & 0xff);
    out.append(b, 4);
}

void putU64BE(std::string& out, u64 v) {
    char b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<char>((v >> (56 - 8 * i)) & 0xff);
    out.append(b, 8);
}

bool readU32BE(absl::string_view s, u32& v) {
    if (s.size() != 4) return false;
    v = (static_cast<u32>(static_cast<u8>(s[0])) << 24) |
        (static_cast<u32>(static_cast<u8>(s[1])) << 16) |
        (static_cast<u32>(static_cast<u8>(s[2])) << 8)  |
        (static_cast<u32>(static_cast<u8>(s[3])));
    return true;
}

bool readU64BE(absl::string_view s, u64& v) {
    if (s.size() != 8) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<u8>(s[i]);
    return true;
}

// ---------------- decoders ----------------

namespace {

StatusOr<u64> decodeSid(const std::string& s) {
    u64 v;
    if (!readU64BE(s, v)) return absl::InvalidArgumentError("bad sid");
    return v;
}

StatusOr<u32> decodeU32(const std::string& s) {
    u32 v;
    if (!readU32BE(s, v)) return absl::InvalidArgumentError("bad u32");
    return v;
}

} // namespace

StatusOr<ShineFrame> decodeShineFrame(const Resp2Frame& rf) {
    if (std::holds_alternative<Resp2BulkFrame>(rf)) {
        const auto& b = std::get<Resp2BulkFrame>(rf);
        if (b.bulk.size < 1 + 8 || b.bulk.data[0] != kDataMagic) {
            return absl::InvalidArgumentError("bad DATA frame header");
        }
        DataFrame df;
        df.sid = 0;
        for (int i = 0; i < 8; ++i) df.sid = (df.sid << 8) | b.bulk.data[1 + i];
        df.payload = b.bulk.data + 9;
        df.size    = b.bulk.size - 9;
        return ShineFrame{df};
    }
    if (!std::holds_alternative<Resp2ArrayFrame>(rf)) {
        return absl::InvalidArgumentError("empty frame");
    }
    const auto& arr = std::get<Resp2ArrayFrame>(rf).elements;
    if (arr.empty()) return absl::InvalidArgumentError("empty array frame");
    const std::string& cmd = arr[0];

    auto need = [&](std::size_t n) -> Status {
        if (arr.size() < n) return absl::InvalidArgumentError(
            absl::StrCat("command ", cmd, " needs ", n - 1, " args"));
        return absl::OkStatus();
    };

    if (cmd == "HELLO") {
        if (auto s = need(3); !s.ok()) return s;
        return ShineFrame{HelloFrame{arr[1], arr[2]}};
    }
    if (cmd == "AUTH") {
        if (auto s = need(2); !s.ok()) return s;
        return ShineFrame{AuthFrame{arr[1]}};
    }
    if (cmd == "OK")  return ShineFrame{OkFrame{}};
    if (cmd == "ERR") {
        if (auto s = need(3); !s.ok()) return s;
        return ShineFrame{ErrFrame{arr[1], arr[2]}};
    }
    if (cmd == "NEW") {
        if (auto s = need(4); !s.ok()) return s;
        auto sid = decodeSid(arr[1]); if (!sid.ok()) return sid.status();
        auto win = decodeU32(arr[3]); if (!win.ok()) return win.status();
        return ShineFrame{NewFrame{*sid, arr[2], *win}};
    }
    if (cmd == "ACK") {
        if (auto s = need(2); !s.ok()) return s;
        auto sid = decodeSid(arr[1]); if (!sid.ok()) return sid.status();
        return ShineFrame{AckFrame{*sid}};
    }
    if (cmd == "CLOSE") {
        if (auto s = need(2); !s.ok()) return s;
        auto sid = decodeSid(arr[1]); if (!sid.ok()) return sid.status();
        std::string reason = arr.size() >= 3 ? arr[2] : "";
        return ShineFrame{CloseFrame{*sid, std::move(reason)}};
    }
    if (cmd == "RESET") {
        if (auto s = need(2); !s.ok()) return s;
        auto sid = decodeSid(arr[1]); if (!sid.ok()) return sid.status();
        return ShineFrame{ResetFrame{*sid}};
    }
    if (cmd == "WINDOW") {
        if (auto s = need(3); !s.ok()) return s;
        auto sid = decodeSid(arr[1]); if (!sid.ok()) return sid.status();
        auto dlt = decodeU32(arr[2]); if (!dlt.ok()) return dlt.status();
        return ShineFrame{WindowFrame{*sid, *dlt}};
    }
    if (cmd == "PING") {
        if (auto s = need(2); !s.ok()) return s;
        auto n = decodeSid(arr[1]); if (!n.ok()) return n.status();
        return ShineFrame{PingFrame{*n}};
    }
    if (cmd == "PONG") {
        if (auto s = need(2); !s.ok()) return s;
        auto n = decodeSid(arr[1]); if (!n.ok()) return n.status();
        return ShineFrame{PongFrame{*n}};
    }
    return absl::InvalidArgumentError(absl::StrCat("unknown cmd: ", cmd));
}

// ---------------- encoders ----------------

void encodeHello(std::string& out, const HelloFrame& f) {
    const absl::string_view parts[] = {"HELLO", sv(f.version), sv(f.features)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeAuth(std::string& out, const AuthFrame& f) {
    const absl::string_view parts[] = {"AUTH", sv(f.password)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeOk(std::string& out) {
    const absl::string_view parts[] = {"OK"};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeErr(std::string& out, const ErrFrame& f) {
    const absl::string_view parts[] = {"ERR", sv(f.code), sv(f.message)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeNew(std::string& out, const NewFrame& f) {
    std::string sid; putU64BE(sid, f.sid);
    std::string win; putU32BE(win, f.init_window);
    const absl::string_view parts[] = {"NEW", sv(sid), sv(f.addr_blob), sv(win)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeAck(std::string& out, const AckFrame& f) {
    std::string sid; putU64BE(sid, f.sid);
    const absl::string_view parts[] = {"ACK", sv(sid)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeClose(std::string& out, const CloseFrame& f) {
    std::string sid; putU64BE(sid, f.sid);
    const absl::string_view parts[] = {"CLOSE", sv(sid), sv(f.reason)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeReset(std::string& out, const ResetFrame& f) {
    std::string sid; putU64BE(sid, f.sid);
    const absl::string_view parts[] = {"RESET", sv(sid)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeWindow(std::string& out, const WindowFrame& f) {
    std::string sid; putU64BE(sid, f.sid);
    std::string dlt; putU32BE(dlt, f.delta);
    const absl::string_view parts[] = {"WINDOW", sv(sid), sv(dlt)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodePing(std::string& out, const PingFrame& f) {
    std::string n; putU64BE(n, f.nonce);
    const absl::string_view parts[] = {"PING", sv(n)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodePong(std::string& out, const PongFrame& f) {
    std::string n; putU64BE(n, f.nonce);
    const absl::string_view parts[] = {"PONG", sv(n)};
    Resp2Writer::writeArray(out, absl::MakeConstSpan(parts));
}

void encodeData(std::string& out, u64 sid, const u8* payload, std::size_t len) {
    std::size_t bulk_len = 1 + 8 + len;
    absl::StrAppend(&out, "$", bulk_len, "\r\n");
    out.push_back(static_cast<char>(kDataMagic));
    char sid_buf[8];
    for (int i = 0; i < 8; ++i) sid_buf[i] = static_cast<char>((sid >> (56 - 8 * i)) & 0xff);
    out.append(sid_buf, 8);
    out.append(reinterpret_cast<const char*>(payload), len);
    out.append("\r\n", 2);
}

} // namespace shine::proto
