#include "config/schema.hpp"
#include "route/router.hpp"

#include <gtest/gtest.h>

using namespace shine;

namespace {

SessionRequest makeReq(std::string inbound, Address t) {
    SessionRequest r;
    r.inbound_tag = std::move(inbound);
    r.target      = std::move(t);
    return r;
}

} // namespace

TEST(Router, DomainSuffix) {
    config::RouteConfig cfg;
    config::RuleConfig r1;
    r1.domain_suffix = {".cn"};
    r1.outbound_tag  = "direct";
    cfg.rules.push_back(r1);
    cfg.default_outbound = "us";

    auto rt = Router::compile(cfg);
    ASSERT_TRUE(rt.ok());

    EXPECT_EQ(rt->pick(makeReq("in", Address::fromDomain("weibo.cn", 80))), "direct");
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromDomain("example.com", 80))), "us");
}

TEST(Router, InboundTagRule) {
    config::RouteConfig cfg;
    config::RuleConfig r1;
    r1.inbound_tag = "shine-in";
    r1.outbound_tag = "direct";
    cfg.rules.push_back(r1);
    cfg.default_outbound = "us";
    auto rt = Router::compile(cfg); ASSERT_TRUE(rt.ok());

    EXPECT_EQ(rt->pick(makeReq("shine-in", Address::fromDomain("any.com", 80))), "direct");
    EXPECT_EQ(rt->pick(makeReq("s5", Address::fromDomain("any.com", 80))), "us");
}

TEST(Router, CidrV4) {
    config::RouteConfig cfg;
    config::RuleConfig r1;
    r1.cidrs = {"127.0.0.0/8", "10.0.0.0/8"};
    r1.outbound_tag = "direct";
    cfg.rules.push_back(r1);
    cfg.default_outbound = "us";
    auto rt = Router::compile(cfg); ASSERT_TRUE(rt.ok());

    EXPECT_EQ(rt->pick(makeReq("in", Address::fromIPv4({127, 0, 0, 1}, 80))), "direct");
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromIPv4({8, 8, 8, 8}, 80))), "us");
}
