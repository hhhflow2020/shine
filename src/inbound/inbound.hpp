#pragma once

#include "core/common.hpp"
#include "transport/session_request.hpp"

#include <functional>
#include <memory>

namespace shine {

using SessionRequestHandler =
    std::function<awaitable<void>(SessionRequest)>;

class IInbound {
public:
    virtual ~IInbound() = default;
    virtual Status  start(SessionRequestHandler on_req) = 0;
    virtual void    stop()                              = 0;
    virtual const std::string& tag() const = 0;
    virtual const std::string& protocol() const = 0;
};

using InboundPtr = std::shared_ptr<IInbound>;

} // namespace shine
