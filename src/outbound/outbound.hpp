#pragma once

#include "core/common.hpp"
#include "transport/session_request.hpp"

#include <memory>
#include <string>

namespace shine {

class IOutbound {
public:
    virtual ~IOutbound() = default;
    virtual awaitable<Status> handle(SessionRequest req) = 0;
    virtual void stop() {}
    virtual const std::string& tag() const = 0;
    virtual const std::string& protocol() const = 0;
};

using OutboundPtr = std::shared_ptr<IOutbound>;

} // namespace shine
