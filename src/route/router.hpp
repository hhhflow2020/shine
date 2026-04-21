#pragma once

#include "config/schema.hpp"
#include "core/common.hpp"
#include "transport/session_request.hpp"

#include <absl/container/flat_hash_set.h>
#include <absl/strings/string_view.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace shine {

struct IpRange {
    // Stored as normalized bytes (4B for v4, 16B for v6).
    std::array<u8, 16> net{};
    u8                 prefix_len = 0;
    bool               is_v6      = false;

    bool contains(const u8* addr, bool v6) const noexcept;
};

struct CompiledRule {
    std::optional<std::string>                inbound_tag;
    absl::flat_hash_set<std::string>          domain_exact;
    std::vector<std::string>                  domain_suffix;
    std::vector<IpRange>                      cidrs;
    std::string                               outbound_tag;

    bool matches(const SessionRequest& r) const noexcept;
};

class Router {
public:
    static StatusOr<Router> compile(const config::RouteConfig& cfg);

    absl::string_view pick(const SessionRequest& r) const noexcept;

    const std::string& defaultOutbound() const noexcept { return default_outbound_; }

private:
    std::vector<CompiledRule> rules_;
    std::string               default_outbound_;
};

} // namespace shine
