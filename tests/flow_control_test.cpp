#include "proto/shine_frame.hpp"

#include <gtest/gtest.h>

using namespace shine;
using namespace shine::proto;

TEST(FlowControl, BEIntRoundtrip) {
    std::string s;
    putU64BE(s, 0x0102030405060708ULL);
    EXPECT_EQ(s.size(), 8u);
    u64 v = 0;
    EXPECT_TRUE(readU64BE(s, v));
    EXPECT_EQ(v, 0x0102030405060708ULL);

    std::string s2;
    putU32BE(s2, 0xAABBCCDDu);
    u32 v2 = 0;
    EXPECT_TRUE(readU32BE(s2, v2));
    EXPECT_EQ(v2, 0xAABBCCDDu);
}

TEST(FlowControl, WindowFrame) {
    std::string wire;
    encodeWindow(wire, WindowFrame{0x1234567890abcdefULL, 65536});
    // Minimal sanity: starts with *3 array.
    EXPECT_EQ(wire.substr(0, 4), "*3\r\n");
}
