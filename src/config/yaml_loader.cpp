#include "config/yaml_loader.hpp"

#include <absl/container/flat_hash_set.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace shine::config {

namespace {

template <typename T>
T getOr(const YAML::Node& n, const std::string& key, T def) {
    if (!n || !n[key]) return def;
    return n[key].as<T>(def);
}

std::string getStr(const YAML::Node& n, const std::string& key,
                   const std::string& def = "") {
    if (!n || !n[key]) return def;
    return n[key].as<std::string>(def);
}

// Parse duration like "15s", "1m", "500ms".
StatusOr<std::chrono::milliseconds> parseDuration(const std::string& s) {
    if (s.empty()) return absl::InvalidArgumentError("empty duration");
    std::size_t idx = 0;
    while (idx < s.size() && (std::isdigit(static_cast<unsigned char>(s[idx])) || s[idx] == '.')) ++idx;
    if (idx == 0) return absl::InvalidArgumentError("bad duration: " + s);
    double val;
    if (!absl::SimpleAtod(s.substr(0, idx), &val)) return absl::InvalidArgumentError("bad number");
    std::string unit = s.substr(idx);
    double ms = 0;
    if (unit == "ms") ms = val;
    else if (unit == "s" || unit.empty()) ms = val * 1000;
    else if (unit == "m") ms = val * 60 * 1000;
    else if (unit == "h") ms = val * 3600 * 1000;
    else return absl::InvalidArgumentError("unknown unit: " + unit);
    return std::chrono::milliseconds(static_cast<std::int64_t>(ms));
}

StatusOr<std::chrono::seconds> parseSeconds(const YAML::Node& n, const std::string& key,
                                            std::chrono::seconds def) {
    if (!n || !n[key]) return def;
    auto s = n[key].as<std::string>();
    auto ms = parseDuration(s);
    if (!ms.ok()) return ms.status();
    return std::chrono::duration_cast<std::chrono::seconds>(*ms);
}

// Parse size like "256KB", "512MB".
StatusOr<std::size_t> parseSize(const std::string& s) {
    if (s.empty()) return absl::InvalidArgumentError("empty size");
    std::size_t idx = 0;
    while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) ++idx;
    if (idx == 0) return absl::InvalidArgumentError("bad size: " + s);
    std::int64_t val;
    if (!absl::SimpleAtoi(s.substr(0, idx), &val)) return absl::InvalidArgumentError("bad number");
    std::string unit = s.substr(idx);
    std::size_t mult = 1;
    if (unit == "" || unit == "B") mult = 1;
    else if (unit == "K" || unit == "KB" || unit == "KiB") mult = 1024;
    else if (unit == "M" || unit == "MB" || unit == "MiB") mult = 1024ULL * 1024;
    else if (unit == "G" || unit == "GB" || unit == "GiB") mult = 1024ULL * 1024 * 1024;
    else return absl::InvalidArgumentError("unknown size unit: " + unit);
    return static_cast<std::size_t>(val) * mult;
}

StatusOr<std::size_t> parseSizeNode(const YAML::Node& n, const std::string& key, std::size_t def) {
    if (!n || !n[key]) return def;
    auto s = n[key].as<std::string>();
    return parseSize(s);
}

} // namespace

Status Config::validate() const {
    absl::flat_hash_set<std::string> tags;
    for (const auto& in : inbounds) {
        if (in.tag.empty()) return absl::InvalidArgumentError("inbound tag empty");
        if (!tags.insert("in:" + in.tag).second)
            return absl::InvalidArgumentError("duplicate inbound tag: " + in.tag);
        if (in.listen.empty()) return absl::InvalidArgumentError("inbound " + in.tag + " missing listen");
    }
    if (inbounds.empty()) return absl::InvalidArgumentError("no inbounds configured");

    bool has_direct = false;
    absl::flat_hash_set<std::string> out_tags;
    for (const auto& o : outbounds) {
        if (o.tag.empty()) return absl::InvalidArgumentError("outbound tag empty");
        if (!out_tags.insert(o.tag).second)
            return absl::InvalidArgumentError("duplicate outbound tag: " + o.tag);
        if (o.protocol == "direct") has_direct = true;
    }
    if (!has_direct) {
        return absl::InvalidArgumentError("at least one direct outbound is required");
    }
    if (!route.default_outbound.empty() && !out_tags.contains(route.default_outbound)) {
        return absl::InvalidArgumentError("default outbound not found: " + route.default_outbound);
    }
    for (const auto& r : route.rules) {
        if (!out_tags.contains(r.outbound_tag)) {
            return absl::InvalidArgumentError("rule outbound not found: " + r.outbound_tag);
        }
    }
    return absl::OkStatus();
}

static StatusOr<InboundConfig> parseInbound(const YAML::Node& n) {
    InboundConfig c;
    c.tag      = getStr(n, "tag");
    c.protocol = getStr(n, "protocol");
    c.listen   = getStr(n, "listen");
    c.password = getStr(n, "password");
    c.user     = getStr(n, "user");
    c.pass     = getStr(n, "pass");
    auto mp = parseSizeNode(n, "max_payload", c.max_payload);
    if (!mp.ok()) return mp.status();
    c.max_payload = *mp;
    auto iw = parseSizeNode(n, "init_window", c.init_window);
    if (!iw.ok()) return iw.status();
    c.init_window = static_cast<u32>(*iw);
    auto pi = parseSeconds(n, "ping_interval", c.ping_interval);
    if (!pi.ok()) return pi.status();
    c.ping_interval = *pi;
    auto pt = parseSeconds(n, "pong_timeout", c.pong_timeout);
    if (!pt.ok()) return pt.status();
    c.pong_timeout = *pt;
    auto ht = parseSeconds(n, "handshake_timeout", c.handshake_timeout);
    if (!ht.ok()) return ht.status();
    c.handshake_timeout = *ht;
    return c;
}

static StatusOr<OutboundConfig> parseOutbound(const YAML::Node& n) {
    OutboundConfig c;
    c.tag      = getStr(n, "tag");
    c.protocol = getStr(n, "protocol");
    c.server   = getStr(n, "server");
    c.password = getStr(n, "password");
    c.user     = getStr(n, "user");
    c.pass     = getStr(n, "pass");
    c.pool_size = getOr<int>(n, "pool_size", 1);
    auto iw = parseSizeNode(n, "init_window", c.init_window);
    if (!iw.ok()) return iw.status();
    c.init_window = static_cast<u32>(*iw);
    auto pi = parseSeconds(n, "ping_interval", c.ping_interval);
    if (!pi.ok()) return pi.status();
    c.ping_interval = *pi;
    auto pt = parseSeconds(n, "pong_timeout", c.pong_timeout);
    if (!pt.ok()) return pt.status();
    c.pong_timeout = *pt;
    auto ht = parseSeconds(n, "handshake_timeout", c.handshake_timeout);
    if (!ht.ok()) return ht.status();
    c.handshake_timeout = *ht;
    return c;
}

static RuleConfig parseRule(const YAML::Node& n) {
    RuleConfig r;
    if (n["inbound"]) r.inbound_tag = n["inbound"].as<std::string>();
    if (n["domain"])  {
        for (auto x : n["domain"]) r.domain_exact.push_back(x.as<std::string>());
    }
    if (n["domain_suffix"]) {
        for (auto x : n["domain_suffix"]) r.domain_suffix.push_back(x.as<std::string>());
    }
    if (n["cidr"]) {
        for (auto x : n["cidr"]) r.cidrs.push_back(x.as<std::string>());
    }
    r.outbound_tag = getStr(n, "outbound");
    return r;
}

StatusOr<Config> loadFromString(const std::string& content) {
    try {
        Config c;
        YAML::Node root = YAML::Load(content);
        if (auto l = root["log"]) {
            c.log.level  = getStr(l, "level",  c.log.level);
            c.log.format = getStr(l, "format", c.log.format);
        }
        if (auto s = root["server"]) {
            c.server.io_threads = getOr<int>(s, "io_threads", 0);
            auto gt = parseSeconds(s, "graceful_timeout", c.server.graceful_timeout);
            if (!gt.ok()) return gt.status();
            c.server.graceful_timeout = *gt;
        }
        if (auto m = root["metrics"]) {
            c.metrics.listen = getStr(m, "listen", c.metrics.listen);
            c.metrics.path   = getStr(m, "path",   c.metrics.path);
        }
        if (auto ins = root["inbounds"]) {
            for (auto n : ins) {
                auto i = parseInbound(n);
                if (!i.ok()) return i.status();
                c.inbounds.push_back(std::move(*i));
            }
        }
        if (auto outs = root["outbounds"]) {
            for (auto n : outs) {
                auto o = parseOutbound(n);
                if (!o.ok()) return o.status();
                c.outbounds.push_back(std::move(*o));
            }
        }
        if (auto r = root["route"]) {
            if (auto rr = r["rules"]) {
                for (auto n : rr) c.route.rules.push_back(parseRule(n));
            }
            c.route.default_outbound = getStr(r, "default");
        }
        if (auto st = c.validate(); !st.ok()) return st;
        return c;
    } catch (const YAML::Exception& e) {
        return absl::InvalidArgumentError(std::string("yaml parse: ") + e.what());
    }
}

StatusOr<Config> loadFromFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return absl::NotFoundError("cannot open config file: " + path);
    std::ostringstream oss; oss << ifs.rdbuf();
    return loadFromString(oss.str());
}

} // namespace shine::config
