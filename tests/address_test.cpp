#include "transport/address.hpp"

#include <gtest/gtest.h>

using namespace shine;

TEST(Address, ParseHostPortV4) {
    auto a = Address::parseHostPort("192.168.1.1:80");
    ASSERT_TRUE(a.ok());
    EXPECT_EQ(a->type(), Address::Type::IPv4);
    EXPECT_EQ(a->port(), 80);
    EXPECT_EQ(a->toString(), "192.168.1.1:80");
}

TEST(Address, ParseHostPortV6) {
    auto a = Address::parseHostPort("[2001:db8::1]:443");
    ASSERT_TRUE(a.ok());
    EXPECT_EQ(a->type(), Address::Type::IPv6);
    EXPECT_EQ(a->port(), 443);
}

TEST(Address, ParseHostPortDomain) {
    auto a = Address::parseHostPort("example.com:443");
    ASSERT_TRUE(a.ok());
    EXPECT_EQ(a->type(), Address::Type::Domain);
    EXPECT_EQ(a->domain(), "example.com");
    EXPECT_EQ(a->port(), 443);
}

TEST(Address, BinaryRoundtrip) {
    auto a = Address::fromDomain("example.com", 443);
    auto bin = a.toBinary();
    auto b = Address::parseBinary(bin);
    ASSERT_TRUE(b.ok());
    EXPECT_EQ(b->toString(), a.toString());
}

TEST(Address, BinaryIPv6) {
    std::array<u8, 16> bytes{0x20, 0x01, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 1};
    auto a = Address::fromIPv6(bytes, 8080);
    auto bin = a.toBinary();
    EXPECT_EQ(bin[0], 0x02);
    EXPECT_EQ(static_cast<u8>(bin[1]), 16u);
    auto b = Address::parseBinary(bin);
    ASSERT_TRUE(b.ok());
    EXPECT_EQ(b->port(), 8080);
}
