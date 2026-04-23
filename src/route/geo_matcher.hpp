#pragma once

#include "core/common.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/numeric/int128.h>
#include <absl/strings/string_view.h>

#include <regex>
#include <string>
#include <vector>

namespace shine {

class GeoSiteMatcher {
public:
    Status load(const std::string& dat_path);

    bool match(absl::string_view domain, absl::string_view country_code) const;

private:
    struct Category {
        absl::flat_hash_set<std::string> exact;
        absl::flat_hash_set<std::string> suffix;
        std::vector<std::regex>          regexes;
    };
    absl::flat_hash_map<std::string, Category> categories_;
};

class GeoIpMatcher {
public:
    Status load(const std::string& dat_path);

    bool match(const u8* ip, bool is_v6, absl::string_view country_code) const;

private:
    struct RangeV4 {
        u32 start, end;
        bool operator<(const RangeV4& o) const { return start < o.start; }
    };
    struct RangeV6 {
        absl::uint128 start, end;
        bool operator<(const RangeV6& o) const { return start < o.start; }
    };

    struct Category {
        std::vector<RangeV4> v4;
        std::vector<RangeV6> v6;
    };
    absl::flat_hash_map<std::string, Category> categories_;
};

} // namespace shine
