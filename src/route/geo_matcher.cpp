#include "route/geo_matcher.hpp"
#include "src/proto/v2ray.pb.h"
#include "core/logging.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace shine {

Status GeoSiteMatcher::load(const std::string& dat_path) {
    std::ifstream ifs(dat_path, std::ios::binary);
    if (!ifs) return absl::NotFoundError("cannot open geosite.dat: " + dat_path);

    v2ray::core::app::router::routercommon::GeoSiteList list;
    if (!list.ParseFromIstream(&ifs)) {
        return absl::InvalidArgumentError("failed to parse geosite.dat protobuf");
    }

    for (const auto& site : list.entry()) {
        std::string code = site.country_code();
        std::transform(code.begin(), code.end(), code.begin(), ::tolower);
        auto& cat = categories_[code];

        for (const auto& d : site.domain()) {
            if (d.type() == v2ray::core::app::router::routercommon::Domain_Type_Full) {
                cat.exact.insert(d.value());
            } else if (d.type() == v2ray::core::app::router::routercommon::Domain_Type_RootDomain) {
                cat.suffix.insert(d.value());
            } else if (d.type() == v2ray::core::app::router::routercommon::Domain_Type_Regex) {
                try {
                    cat.regexes.emplace_back(d.value());
                } catch (const std::regex_error& e) {
                    SHINE_WARN("geosite regex error '{}': {}", d.value(), e.what());
                }
            } else if (d.type() == v2ray::core::app::router::routercommon::Domain_Type_Plain) {
                // Plain is keyword match (contains), we can treat it as suffix for now or just generic regex
                try {
                    cat.regexes.emplace_back(d.value());
                } catch (...) {}
            }
        }
    }
    SHINE_INFO("loaded geosite from {}, categories: {}", dat_path, categories_.size());
    return absl::OkStatus();
}

bool GeoSiteMatcher::match(absl::string_view domain, absl::string_view country_code) const {
    auto it = categories_.find(std::string(country_code));
    if (it == categories_.end()) return false;
    const auto& cat = it->second;

    if (cat.exact.contains(domain)) return true;

    absl::string_view d = domain;
    while (true) {
        if (cat.suffix.contains(d)) return true;
        auto dot = d.find('.');
        if (dot == absl::string_view::npos) break;
        d = d.substr(dot + 1);
    }

    for (const auto& r : cat.regexes) {
        if (std::regex_search(std::string(domain), r)) return true;
    }
    return false;
}

namespace {
u32 toU32(const std::string& s) {
    if (s.size() != 4) return 0;
    const u8* p = reinterpret_cast<const u8*>(s.data());
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}
absl::uint128 toU128(const std::string& s) {
    if (s.size() != 16) return 0;
    const u8* p = reinterpret_cast<const u8*>(s.data());
    u64 hi = 0, lo = 0;
    for(int i=0; i<8; ++i) hi = (hi << 8) | p[i];
    for(int i=8; i<16; ++i) lo = (lo << 8) | p[i];
    return absl::MakeUint128(hi, lo);
}
} // namespace

Status GeoIpMatcher::load(const std::string& dat_path) {
    std::ifstream ifs(dat_path, std::ios::binary);
    if (!ifs) return absl::NotFoundError("cannot open geoip.dat: " + dat_path);

    v2ray::core::app::router::routercommon::GeoIPList list;
    if (!list.ParseFromIstream(&ifs)) {
        return absl::InvalidArgumentError("failed to parse geoip.dat protobuf");
    }

    for (const auto& geoip : list.entry()) {
        std::string code = geoip.country_code();
        std::transform(code.begin(), code.end(), code.begin(), ::tolower);
        auto& cat = categories_[code];

        for (const auto& cidr : geoip.cidr()) {
            const auto& ip = cidr.ip();
            if (ip.size() == 4) {
                u32 val = toU32(ip);
                u32 mask = cidr.prefix() == 0 ? 0 : (~0U) << (32 - cidr.prefix());
                val &= mask;
                u32 end = val | (~mask);
                cat.v4.push_back({val, end});
            } else if (ip.size() == 16) {
                absl::uint128 val = toU128(ip);
                absl::uint128 mask;
                if (cidr.prefix() == 0) mask = 0;
                else mask = (~absl::uint128(0)) << (128 - cidr.prefix());
                val &= mask;
                absl::uint128 end = val | (~mask);
                cat.v6.push_back({val, end});
            }
        }
    }

    // Sort and merge intervals
    for (auto& [code, cat] : categories_) {
        if (!cat.v4.empty()) {
            std::sort(cat.v4.begin(), cat.v4.end());
            std::vector<RangeV4> merged;
            merged.push_back(cat.v4[0]);
            for (size_t i = 1; i < cat.v4.size(); ++i) {
                auto& last = merged.back();
                if (cat.v4[i].start <= last.end + (last.end != ~0U ? 1 : 0)) {
                    last.end = std::max(last.end, cat.v4[i].end);
                } else {
                    merged.push_back(cat.v4[i]);
                }
            }
            cat.v4 = std::move(merged);
        }
        if (!cat.v6.empty()) {
            std::sort(cat.v6.begin(), cat.v6.end());
            std::vector<RangeV6> merged;
            merged.push_back(cat.v6[0]);
            for (size_t i = 1; i < cat.v6.size(); ++i) {
                auto& last = merged.back();
                if (cat.v6[i].start <= last.end + (last.end != ~absl::uint128(0) ? 1 : 0)) {
                    last.end = std::max(last.end, cat.v6[i].end);
                } else {
                    merged.push_back(cat.v6[i]);
                }
            }
            cat.v6 = std::move(merged);
        }
    }

    SHINE_INFO("loaded geoip from {}, categories: {}", dat_path, categories_.size());
    return absl::OkStatus();
}

bool GeoIpMatcher::match(const u8* ip, bool is_v6, absl::string_view country_code) const {
    auto it = categories_.find(std::string(country_code));
    if (it == categories_.end()) return false;
    const auto& cat = it->second;

    if (!is_v6) {
        u32 val = (static_cast<u32>(ip[0]) << 24) | (static_cast<u32>(ip[1]) << 16) |
                  (static_cast<u32>(ip[2]) << 8) | static_cast<u32>(ip[3]);
        auto upper = std::upper_bound(cat.v4.begin(), cat.v4.end(), RangeV4{val, val});
        if (upper != cat.v4.begin()) {
            --upper;
            if (upper->start <= val && val <= upper->end) return true;
        }
    } else {
        u64 hi = 0, lo = 0;
        for(int i=0; i<8; ++i) hi = (hi << 8) | ip[i];
        for(int i=8; i<16; ++i) lo = (lo << 8) | ip[i];
        absl::uint128 val = absl::MakeUint128(hi, lo);
        auto upper = std::upper_bound(cat.v6.begin(), cat.v6.end(), RangeV6{val, val});
        if (upper != cat.v6.begin()) {
            --upper;
            if (upper->start <= val && val <= upper->end) return true;
        }
    }
    return false;
}

} // namespace shine
