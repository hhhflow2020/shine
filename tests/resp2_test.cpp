#include "proto/resp2_parser.hpp"
#include "proto/resp2_writer.hpp"

#include <gtest/gtest.h>

using namespace shine;
using namespace shine::proto;

namespace {

void push(ReadBuffer& b, const std::string& s) {
    b.ensure_writable(s.size());
    std::memcpy(b.writable_ptr(), s.data(), s.size());
    b.committed(s.size());
}

} // namespace

TEST(Resp2, ParseArray) {
    Resp2Parser p;
    ReadBuffer rb;
    push(rb, "*3\r\n$5\r\nHELLO\r\n$1\r\n1\r\n$3\r\nmux\r\n");
    Resp2Frame f;
    auto n = p.tryParse(rb, f);
    ASSERT_TRUE(n.ok());
    EXPECT_GT(*n, 0u);
    ASSERT_TRUE(std::holds_alternative<Resp2ArrayFrame>(f));
    const auto& arr = std::get<Resp2ArrayFrame>(f).elements;
    EXPECT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0], "HELLO");
    EXPECT_EQ(arr[1], "1");
    EXPECT_EQ(arr[2], "mux");
}

TEST(Resp2, PartialReturnsZero) {
    Resp2Parser p;
    ReadBuffer rb;
    push(rb, "*2\r\n$5\r\nHELLO\r\n$3\r");   // truncated
    Resp2Frame f;
    auto n = p.tryParse(rb, f);
    ASSERT_TRUE(n.ok());
    EXPECT_EQ(*n, 0u);
}

TEST(Resp2, ParseBulkBinary) {
    Resp2Parser p;
    ReadBuffer rb;
    std::string s;
    // $5 payload [0xD1,0,0,0,0,0,0,0,1], 9 bytes header + 2 content = 11 payload
    std::string payload;
    payload.push_back(char(0xD1));
    for (int i = 0; i < 7; ++i) payload.push_back(0);
    payload.push_back(1);
    payload += "ab";
    s = "$" + std::to_string(payload.size()) + "\r\n" + payload + "\r\n";
    push(rb, s);
    Resp2Frame f;
    auto n = p.tryParse(rb, f);
    ASSERT_TRUE(n.ok());
    EXPECT_GT(*n, 0u);
    ASSERT_TRUE(std::holds_alternative<Resp2BulkFrame>(f));
    EXPECT_EQ(std::get<Resp2BulkFrame>(f).bulk.size, payload.size());
}

TEST(Resp2, MaxBulk) {
    Resp2Parser p(100);
    ReadBuffer rb;
    push(rb, "$200\r\n");
    Resp2Frame f;
    auto n = p.tryParse(rb, f);
    EXPECT_FALSE(n.ok());
}

TEST(Resp2, WriteRoundTrip) {
    std::string out;
    std::vector<std::string> parts = {"HELLO", "1", "mux"};
    Resp2Writer::writeArray(out, parts);
    EXPECT_EQ(out, "*3\r\n$5\r\nHELLO\r\n$1\r\n1\r\n$3\r\nmux\r\n");
}
