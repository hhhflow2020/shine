#include "config/schema.hpp"
#include "route/router.hpp"
#include "proto/v2ray.pb.h"
#include <fstream>

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

TEST(Router, GeoSiteAndGeoIp) {
    // Generate dummy geosite.dat
    {
        v2ray::core::app::router::routercommon::GeoSiteList site_list;
        auto* cn_site = site_list.add_entry();
        cn_site->set_country_code("cn");
        auto* d1 = cn_site->add_domain();
        d1->set_type(v2ray::core::app::router::routercommon::Domain_Type_Full);
        d1->set_value("bilibili.com");
        auto* d2 = cn_site->add_domain();
        d2->set_type(v2ray::core::app::router::routercommon::Domain_Type_RootDomain);
        d2->set_value("qq.com");
        
        std::ofstream ofs("geosite.dat", std::ios::binary);
        site_list.SerializeToOstream(&ofs);
    }

    // Generate dummy geoip.dat
    {
        v2ray::core::app::router::routercommon::GeoIPList ip_list;
        auto* cn_ip = ip_list.add_entry();
        cn_ip->set_country_code("cn");
        auto* c1 = cn_ip->add_cidr();
        c1->set_ip("\x01\x02\x03\x00", 4);
        c1->set_prefix(24);
        
        std::ofstream ofs("geoip.dat", std::ios::binary);
        ip_list.SerializeToOstream(&ofs);
    }

    config::RouteConfig cfg;
    cfg.geosite_path = "geosite.dat";
    cfg.geoip_path = "geoip.dat";
    
    // Rule for geosite:cn -> direct
    config::RuleConfig r1;
    r1.geosite = {"cn"};
    r1.outbound_tag = "direct";
    cfg.rules.push_back(r1);
    
    // Rule for geoip:cn -> block
    config::RuleConfig r2;
    r2.geoip = {"cn"};
    r2.outbound_tag = "block";
    cfg.rules.push_back(r2);
    
    cfg.default_outbound = "proxy";
    auto rt = Router::compile(cfg); ASSERT_TRUE(rt.ok());

    // Matches geosite "cn" (Full) -> direct
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromDomain("bilibili.com", 80))), "direct");
    // Matches geosite "cn" (RootDomain) -> direct
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromDomain("www.qq.com", 80))), "direct");
    // Matches geoip "cn" -> block
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromIPv4({1, 2, 3, 100}, 80))), "block");
    
    // Unmatched domain -> proxy
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromDomain("google.com", 80))), "proxy");
    // Unmatched IP -> proxy
    EXPECT_EQ(rt->pick(makeReq("in", Address::fromIPv4({8, 8, 8, 8}, 80))), "proxy");
}
