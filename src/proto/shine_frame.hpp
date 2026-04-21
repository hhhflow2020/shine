#pragma once

#include "core/buffer.hpp"
#include "core/common.hpp"
#include "proto/resp2_parser.hpp"

#include <absl/strings/string_view.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shine::proto {

// Shine protocol frame types. DATA uses the compact BulkString path; the rest
// travel as RESP2 arrays with a command tag as first element.
enum class FrameType : u8 {
    Hello   = 1,
    Auth    = 2,
    Ok      = 3,
    Err     = 4,
    New     = 5,
    Ack     = 6,
    Close   = 7,
    Reset   = 8,
    Window  = 9,
    Ping    = 10,
    Pong    = 11,
    Data    = 12,
};

// The wire magic byte that identifies the compact DATA frame inside the
// RESP2 BulkString payload.
static constexpr u8 kDataMagic = 0xD1;

struct HelloFrame  { std::string version; std::string features; };
struct AuthFrame   { std::string password; };
struct OkFrame     {};
struct ErrFrame    { std::string code; std::string message; };
struct NewFrame    { u64 sid; std::string addr_blob; u32 init_window; };
struct AckFrame    { u64 sid; };
struct CloseFrame  { u64 sid; std::string reason; };
struct ResetFrame  { u64 sid; };
struct WindowFrame { u64 sid; u32 delta; };
struct PingFrame   { u64 nonce; };
struct PongFrame   { u64 nonce; };
// DATA frame references data by pointer+len valid until the owning read buffer
// is consumed. Copy if you need to keep it.
struct DataFrame {
    u64         sid;
    const u8*   payload;
    std::size_t size;
};

using ShineFrame = std::variant<
    HelloFrame, AuthFrame, OkFrame, ErrFrame, NewFrame, AckFrame,
    CloseFrame, ResetFrame, WindowFrame, PingFrame, PongFrame, DataFrame>;

// Decode a parsed RESP2 frame (either Array or Bulk) into a ShineFrame.
StatusOr<ShineFrame> decodeShineFrame(const Resp2Frame& rf);

// Encoders: append the RESP2 wire bytes to `out`. DATA uses writeBulk directly
// since the wire payload is (magic + sid + user_bytes).
void encodeHello (std::string& out, const HelloFrame&  f);
void encodeAuth  (std::string& out, const AuthFrame&   f);
void encodeOk    (std::string& out);
void encodeErr   (std::string& out, const ErrFrame&    f);
void encodeNew   (std::string& out, const NewFrame&    f);
void encodeAck   (std::string& out, const AckFrame&    f);
void encodeClose (std::string& out, const CloseFrame&  f);
void encodeReset (std::string& out, const ResetFrame&  f);
void encodeWindow(std::string& out, const WindowFrame& f);
void encodePing  (std::string& out, const PingFrame&   f);
void encodePong  (std::string& out, const PongFrame&   f);

// DATA: emits the framing only; caller appends body bytes then "\r\n".
// Returns total bulk length (header bytes written + promised payload).
// To write in one go with a known payload:
void encodeData(std::string& out, u64 sid, const u8* payload, std::size_t len);

// Fixed-size binary helpers (big-endian).
void putU32BE(std::string& out, u32 v);
void putU64BE(std::string& out, u64 v);
bool readU32BE(absl::string_view s, u32& v);
bool readU64BE(absl::string_view s, u64& v);

} // namespace shine::proto
