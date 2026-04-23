#include "proto/resp2_parser.hpp"
#include "proto/shine_frame.hpp"

#include <gtest/gtest.h>

using namespace shine;
using namespace shine::proto;

namespace {
void push(ReadBuffer& b, const std::string& s) {
    b.ensure_writable(s.size());
    std::memcpy(b.writable_ptr(), s.data(), s.size());
    b.committed(s.size());
}
}

TEST(ShineFrame, NewRoundtrip) {
    std::string wire;
    NewFrame nf;
    nf.sid         = 0xdeadbeefcafebabeULL;
    nf.addr_blob   = "\x03\x04test\x01\xbb";
    nf.init_window = 262144;
    encodeNew(wire, nf);

    Resp2Parser p;
    ReadBuffer rb;
    push(rb, wire);
    Resp2Frame rf;
    auto n = p.tryParse(rb, rf); ASSERT_TRUE(n.ok()); EXPECT_GT(*n, 0u);
    auto sf = decodeShineFrame(rf); ASSERT_TRUE(sf.ok());
    ASSERT_TRUE(std::holds_alternative<NewFrame>(*sf));
    const auto& g = std::get<NewFrame>(*sf);
    EXPECT_EQ(g.sid, nf.sid);
    EXPECT_EQ(g.addr_blob, nf.addr_blob);
    EXPECT_EQ(g.init_window, nf.init_window);
}

TEST(ShineFrame, DataRoundtrip) {
    std::string wire;
    std::string payload = "hello world";
    encodeData(wire, 0x1122334455667788ULL,
               reinterpret_cast<const u8*>(payload.data()), payload.size());

    Resp2Parser p;
    ReadBuffer rb;
    push(rb, wire);
    Resp2Frame rf;
    auto n = p.tryParse(rb, rf); ASSERT_TRUE(n.ok()); EXPECT_GT(*n, 0u);
    auto sf = decodeShineFrame(rf); ASSERT_TRUE(sf.ok());
    ASSERT_TRUE(std::holds_alternative<DataFrame>(*sf));
    const auto& d = std::get<DataFrame>(*sf);
    EXPECT_EQ(d.sid, 0x1122334455667788ULL);
    EXPECT_EQ(d.size, payload.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(d.payload), d.size), payload);
}

TEST(ShineFrame, PingPongRoundtrip) {
    std::string wire;
    encodePing(wire, PingFrame{0x1234567890abcdefULL});

    Resp2Parser p;
    ReadBuffer rb;
    push(rb, wire);
    Resp2Frame rf;
    auto n = p.tryParse(rb, rf); ASSERT_TRUE(n.ok());
    auto sf = decodeShineFrame(rf); ASSERT_TRUE(sf.ok());
    ASSERT_TRUE(std::holds_alternative<PingFrame>(*sf));
    EXPECT_EQ(std::get<PingFrame>(*sf).nonce, 0x1234567890abcdefULL);
}

TEST(ShineFrame, NewUdpRoundtrip) {
    std::string wire;
    NewUdpFrame f;
    f.sid = 0xabcdef1234567890ULL;
    encodeNewUdp(wire, f);

    Resp2Parser p;
    ReadBuffer rb;
    push(rb, wire);
    Resp2Frame rf;
    auto n = p.tryParse(rb, rf); ASSERT_TRUE(n.ok()); EXPECT_GT(*n, 0u);
    auto sf = decodeShineFrame(rf); ASSERT_TRUE(sf.ok());
    ASSERT_TRUE(std::holds_alternative<NewUdpFrame>(*sf));
    EXPECT_EQ(std::get<NewUdpFrame>(*sf).sid, f.sid);
}

TEST(ShineFrame, UdpDataRoundtrip) {
    std::string wire;
    UdpDataFrame f;
    f.sid = 0x9988776655443322ULL;
    f.addr_blob = "\x01\x08\x08\x08\x08\x00\x35"; // 8.8.8.8:53
    f.payload = "dns_query_bytes";
    encodeUdpData(wire, f);

    Resp2Parser p;
    ReadBuffer rb;
    push(rb, wire);
    Resp2Frame rf;
    auto n = p.tryParse(rb, rf); ASSERT_TRUE(n.ok()); EXPECT_GT(*n, 0u);
    auto sf = decodeShineFrame(rf); ASSERT_TRUE(sf.ok());
    ASSERT_TRUE(std::holds_alternative<UdpDataFrame>(*sf));
    const auto& d = std::get<UdpDataFrame>(*sf);
    EXPECT_EQ(d.sid, f.sid);
    EXPECT_EQ(d.addr_blob, f.addr_blob);
    EXPECT_EQ(d.payload, f.payload);
}
