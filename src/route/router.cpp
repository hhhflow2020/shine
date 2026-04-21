#include "route/router.hpp"

#include "transport/address.hpp"

#include <absl/strings/match.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_split.h>

#include <boost/asio/ip/address.hpp>

#include <cstring>

namespace shine {

namespace {

StatusOr<IpRange> parseCidr(absl::string_view s) {
    auto slash = s.find('/');
    int prefix = -1;
    absl::string_view host = s;
    if (slash != absl::string_view::npos) {
        host = s.substr(0, slash);
        if (!absl::SimpleAtoi(s.substr(slash + 1), &prefix))
            return absl::InvalidArgumentError("bad cidr prefix");
    }
    boost::system::error_code ec;
    auto addr = boost::asio::ip::make_address(std::string(host), ec);
    if (ec) return absl::InvalidArgumentError("bad ip: " + std::string(host));
    IpRange r;
    if (addr.is_v4()) {
        auto b = addr.to_v4().to_bytes();
        std::memcpy(r.net.data(), b.data(), 4);
        r.is_v6      = false;
        r.prefix_len = prefix < 0 ? 32 : static_cast<u8>(prefix);
        if (r.prefix_len > 32) return absl::InvalidArgumentError("ipv4 prefix > 32");
    } else {
        auto b = addr.to_v6().to_bytes();
        std::memcpy(r.net.data(), b.data(), 16);
        r.is_v6      = true;
        r.prefix_len = prefix < 0 ? 128 : static_cast<u8>(prefix);
        if (r.prefix_len > 128) return absl::InvalidArgumentError("ipv6 prefix > 128");
    }
    return r;
}

bool bitsEqual(const u8* a, const u8* b, u8 prefix) {
    int full = prefix / 8;
    int rem  = prefix % 8;
    if (std::memcmp(a, b, full) != 0) return false;
    if (rem == 0) return true;
    u8 mask = static_cast<u8>(0xFF << (8 - rem));
    return ((a[full] ^ b[full]) & mask) == 0;
}

bool endsWithSuffix(absl::string_view domain, absl::string_view suffix) {
    if (suffix.empty()) return false;
    if (!absl::StrContains(suffix, '.') && absl::EndsWith(domain, suffix)) {
        // allow "cn" to match "*.cn" style when user writes suffix without dot
    }
    if (domain.size() < suffix.size()) return false;
    return absl::EndsWith(domain, suffix) &&
           (domain.size() == suffix.size() ||
            (!suffix.empty() && (suffix.front() == '.' ||
              domain[domain.size() - suffix.size() - 1] == '.')));
}

} // namespace

bool IpRange::contains(const u8* addr, bool v6) const noexcept {
    if (v6 != is_v6) return false;
    return bitsEqual(net.data(), addr, prefix_len);
}

bool CompiledRule::matches(const SessionRequest& r) const noexcept {
    if (inbound_tag && *inbound_tag != r.inbound_tag) return false;
    bool any_rule = false;
    bool any_match = false;
    if (!domain_exact.empty() || !domain_suffix.empty()) {
        any_rule = true;
        if (r.target.isDomain()) {
            const auto& d = r.target.domain();
            if (domain_exact.contains(d)) any_match = true;
            for (const auto& suf : domain_suffix) {
                if (endsWithSuffix(d, suf)) { any_match = true; break; }
            }
        }
    }
    if (!cidrs.empty()) {
        any_rule = true;
        if (r.target.isIP()) {
            bool v6 = (r.target.type() == Address::Type::IPv6);
            const u8* p = r.target.bytes().data();
            for (const auto& cr : cidrs) {
                if (cr.contains(p, v6)) { any_match = true; break; }
            }
        }
    }
    if (!any_rule) {
        // Only inbound_tag specified (or nothing) → success if inbound matched.
        return inbound_tag.has_value();
    }
    return any_match;
}

StatusOr<Router> Router::compile(const config::RouteConfig& cfg) {
    Router r;
    r.default_outbound_ = cfg.default_outbound;
    r.rules_.reserve(cfg.rules.size());
    for (const auto& rc : cfg.rules) {
        CompiledRule cr;
        cr.inbound_tag   = rc.inbound_tag;
        cr.outbound_tag  = rc.outbound_tag;
        for (auto& d : rc.domain_exact) cr.domain_exact.insert(d);
        cr.domain_suffix = rc.domain_suffix;
        for (auto& c : rc.cidrs) {
            auto ip = parseCidr(c);
            if (!ip.ok()) return ip.status();
            cr.cidrs.push_back(*ip);
        }
        r.rules_.push_back(std::move(cr));
    }
    return r;
}

absl::string_view Router::pick(const SessionRequest& r) const noexcept {
    for (const auto& rule : rules_) {
        if (rule.matches(r)) return rule.outbound_tag;
    }
    return default_outbound_;
}

} // namespace shine
