#pragma once

#include "config/schema.hpp"
#include "outbound/outbound.hpp"

namespace shine {

// Drops the inbound stream immediately. Useful for "deny" routing rules.
class BlockOutbound final : public IOutbound {
public:
    explicit BlockOutbound(config::OutboundConfig cfg) : cfg_(std::move(cfg)) {}

    awaitable<Status>  handle(SessionRequest req) override;
    const std::string& tag() const override      { return cfg_.tag; }
    const std::string& protocol() const override { return cfg_.protocol; }

private:
    config::OutboundConfig cfg_;
};

} // namespace shine
