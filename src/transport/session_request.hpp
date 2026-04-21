#pragma once

#include "core/common.hpp"
#include "transport/address.hpp"
#include "transport/session_stream.hpp"

#include <memory>
#include <string>

namespace shine {

// A routing-visible request: "inbound X wants to send bytes to target".
// The actual bytestream is an ISessionStream (either a wrapped tcp::socket or
// a shine Session).
struct SessionRequest {
    std::string                       inbound_tag;
    std::string                       inbound_protocol;
    Address                           target;
    std::shared_ptr<ISessionStream>   client_stream;
};

} // namespace shine
