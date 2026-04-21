#pragma once

#include "config/schema.hpp"
#include "outbound/outbound.hpp"

namespace shine {

class HttpConnectOutbound final : public IOutbound {
public:
    HttpConnectOutbound(asio::any_io_executor ex, config::OutboundConfig cfg)
        : ex_(std::move(ex)), cfg_(std::move(cfg)) {}

    awaitable<Status> handle(SessionRequest req) override;
    const std::string& tag() const override      { return cfg_.tag; }
    const std::string& protocol() const override { return cfg_.protocol; }

private:
    asio::any_io_executor  ex_;
    config::OutboundConfig cfg_;
};

} // namespace shine
