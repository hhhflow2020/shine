#pragma once

#include "core/common.hpp"

#include <absl/strings/string_view.h>

#include <array>
#include <string>

namespace shine {

// Unified target address: IPv4 / IPv6 / Domain, with port.
// Binary wire format (shine proto):
//   <type:1B> <len:1B> <bytes:len> <port:2B BE>
//     type=0x01 IPv4 (len=4)
//     type=0x02 IPv6 (len=16)
//     type=0x03 Domain (len=1..255)
class Address {
public:
    enum class Type : u8 { IPv4 = 1, IPv6 = 2, Domain = 3 };

    Address() = default;

    // Factory helpers.
    static Address fromIPv4(std::array<u8, 4> bytes, u16 port);
    static Address fromIPv6(std::array<u8, 16> bytes, u16 port);
    static Address fromDomain(std::string domain, u16 port);

    // Parse "host:port" / "[ipv6]:port".
    static StatusOr<Address> parseHostPort(absl::string_view s);

    // Parse/serialize to binary wire format used in NEW frame.
    static StatusOr<Address> parseBinary(absl::string_view blob);
    std::string             toBinary() const;

    // Human-readable "host:port"/"[::1]:80"/"example.com:443"
    std::string toString() const;

    Type        type()     const noexcept { return type_; }
    u16         port()     const noexcept { return port_; }
    bool        isIP()     const noexcept { return type_ != Type::Domain; }
    bool        isDomain() const noexcept { return type_ == Type::Domain; }

    const std::string&         domain() const noexcept { return domain_; }
    const std::array<u8, 16>&  bytes()  const noexcept { return bytes_; }

    // Resolve to a tcp::endpoint (uses resolver for domains). For IP, no-op.
    awaitable<StatusOr<tcp::endpoint>> resolveOne() const;

private:
    Type                  type_ = Type::Domain;
    std::array<u8, 16>    bytes_{};    // IPv4 uses first 4
    std::string           domain_;     // only used when Type::Domain
    u16                   port_ = 0;
};

} // namespace shine
