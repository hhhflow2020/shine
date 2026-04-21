#include "transport/address.hpp"

#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>

#include <boost/asio/ip/tcp.hpp>

#include <cstring>

namespace shine {

namespace {

bool parseIPv4(absl::string_view s, std::array<u8, 4>& out) {
    std::vector<std::string> parts = absl::StrSplit(s, '.');
    if (parts.size() != 4) return false;
    for (int i = 0; i < 4; ++i) {
        int v;
        if (!absl::SimpleAtoi(parts[i], &v) || v < 0 || v > 255) return false;
        out[i] = static_cast<u8>(v);
    }
    return true;
}

bool parseIPv6(absl::string_view s, std::array<u8, 16>& out) {
    boost::system::error_code ec;
    auto a = boost::asio::ip::make_address_v6(std::string(s), ec);
    if (ec) return false;
    auto bytes = a.to_bytes();
    std::memcpy(out.data(), bytes.data(), 16);
    return true;
}

} // namespace

Address Address::fromIPv4(std::array<u8, 4> b, u16 port) {
    Address a;
    a.type_ = Type::IPv4;
    std::memcpy(a.bytes_.data(), b.data(), 4);
    a.port_ = port;
    return a;
}

Address Address::fromIPv6(std::array<u8, 16> b, u16 port) {
    Address a;
    a.type_  = Type::IPv6;
    a.bytes_ = b;
    a.port_  = port;
    return a;
}

Address Address::fromDomain(std::string domain, u16 port) {
    Address a;
    a.type_   = Type::Domain;
    a.domain_ = std::move(domain);
    a.port_   = port;
    return a;
}

StatusOr<Address> Address::parseHostPort(absl::string_view s) {
    if (s.empty()) return absl::InvalidArgumentError("empty address");
    absl::string_view host;
    absl::string_view port_s;
    if (s.front() == '[') {
        auto rb = s.find(']');
        if (rb == absl::string_view::npos) return absl::InvalidArgumentError("missing ]");
        host = s.substr(1, rb - 1);
        if (rb + 1 >= s.size() || s[rb + 1] != ':') return absl::InvalidArgumentError("missing :port");
        port_s = s.substr(rb + 2);
        int port;
        if (!absl::SimpleAtoi(port_s, &port) || port <= 0 || port > 65535)
            return absl::InvalidArgumentError("bad port");
        std::array<u8, 16> b;
        if (!parseIPv6(host, b)) return absl::InvalidArgumentError("bad ipv6");
        return fromIPv6(b, static_cast<u16>(port));
    }
    auto colon = s.rfind(':');
    if (colon == absl::string_view::npos) return absl::InvalidArgumentError("missing :port");
    host   = s.substr(0, colon);
    port_s = s.substr(colon + 1);
    int port;
    if (!absl::SimpleAtoi(port_s, &port) || port <= 0 || port > 65535)
        return absl::InvalidArgumentError("bad port");
    std::array<u8, 4> v4;
    if (parseIPv4(host, v4)) return fromIPv4(v4, static_cast<u16>(port));
    return fromDomain(std::string(host), static_cast<u16>(port));
}

StatusOr<Address> Address::parseBinary(absl::string_view blob) {
    if (blob.size() < 4) return absl::InvalidArgumentError("addr blob too short");
    u8 t   = static_cast<u8>(blob[0]);
    u8 len = static_cast<u8>(blob[1]);
    if (blob.size() < static_cast<std::size_t>(2 + len + 2))
        return absl::InvalidArgumentError("addr blob truncated");
    u16 port = (static_cast<u16>(static_cast<u8>(blob[2 + len])) << 8) |
                static_cast<u16>(static_cast<u8>(blob[2 + len + 1]));
    switch (static_cast<Type>(t)) {
        case Type::IPv4: {
            if (len != 4) return absl::InvalidArgumentError("bad ipv4 len");
            std::array<u8, 4> b;
            std::memcpy(b.data(), blob.data() + 2, 4);
            return fromIPv4(b, port);
        }
        case Type::IPv6: {
            if (len != 16) return absl::InvalidArgumentError("bad ipv6 len");
            std::array<u8, 16> b;
            std::memcpy(b.data(), blob.data() + 2, 16);
            return fromIPv6(b, port);
        }
        case Type::Domain: {
            if (len == 0) return absl::InvalidArgumentError("empty domain");
            std::string d(blob.data() + 2, len);
            return fromDomain(std::move(d), port);
        }
    }
    return absl::InvalidArgumentError("unknown addr type");
}

std::string Address::toBinary() const {
    std::string out;
    out.reserve(32);
    u8 len = 0;
    const u8* p = nullptr;
    std::string tmp;
    switch (type_) {
        case Type::IPv4:   len = 4;  p = bytes_.data(); break;
        case Type::IPv6:   len = 16; p = bytes_.data(); break;
        case Type::Domain: len = static_cast<u8>(std::min<std::size_t>(domain_.size(), 255));
                           p = reinterpret_cast<const u8*>(domain_.data()); break;
    }
    out.push_back(static_cast<char>(static_cast<u8>(type_)));
    out.push_back(static_cast<char>(len));
    out.append(reinterpret_cast<const char*>(p), len);
    out.push_back(static_cast<char>((port_ >> 8) & 0xff));
    out.push_back(static_cast<char>(port_ & 0xff));
    return out;
}

std::string Address::toString() const {
    switch (type_) {
        case Type::IPv4: {
            return absl::StrCat(
                static_cast<int>(bytes_[0]), ".", static_cast<int>(bytes_[1]), ".",
                static_cast<int>(bytes_[2]), ".", static_cast<int>(bytes_[3]), ":", port_);
        }
        case Type::IPv6: {
            boost::asio::ip::address_v6::bytes_type b;
            std::memcpy(b.data(), bytes_.data(), 16);
            auto a = boost::asio::ip::make_address_v6(b);
            return absl::StrCat("[", a.to_string(), "]:", port_);
        }
        case Type::Domain:
            return absl::StrCat(domain_, ":", port_);
    }
    return "";
}

awaitable<StatusOr<tcp::endpoint>> Address::resolveOne() const {
    auto ex = co_await asio::this_coro::executor;
    if (type_ == Type::IPv4) {
        boost::asio::ip::address_v4::bytes_type b;
        std::memcpy(b.data(), bytes_.data(), 4);
        co_return tcp::endpoint(boost::asio::ip::make_address_v4(b), port_);
    }
    if (type_ == Type::IPv6) {
        boost::asio::ip::address_v6::bytes_type b;
        std::memcpy(b.data(), bytes_.data(), 16);
        co_return tcp::endpoint(boost::asio::ip::make_address_v6(b), port_);
    }
    tcp::resolver r(ex);
    boost::system::error_code ec;
    auto results = co_await r.async_resolve(
        domain_, std::to_string(port_), asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(absl::StrCat("resolve failed: ", ec.message()));
    if (results.empty()) co_return absl::NotFoundError("no endpoints");
    co_return results.begin()->endpoint();
}

} // namespace shine
