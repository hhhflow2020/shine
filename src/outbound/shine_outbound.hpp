#pragma once

#include "config/schema.hpp"
#include "outbound/outbound.hpp"
#include "transport/link.hpp"

#include <absl/synchronization/mutex.h>

#include <atomic>
#include <memory>
#include <vector>

namespace shine {

class ShineOutbound final : public IOutbound,
                            public std::enable_shared_from_this<ShineOutbound> {
public:
    ShineOutbound(asio::any_io_executor ex, config::OutboundConfig cfg)
        : ex_(std::move(ex)), cfg_(std::move(cfg)) {}

    awaitable<Status>  handle(SessionRequest req) override;
    void               stop() override;
    const std::string& tag() const override      { return cfg_.tag; }
    const std::string& protocol() const override { return cfg_.protocol; }

private:
    awaitable<LinkPtr> pickOrMakeLink();
    awaitable<LinkPtr> dialNewLink();

    asio::any_io_executor       ex_;
    config::OutboundConfig      cfg_;
    std::vector<LinkPtr>        pool_;
    absl::Mutex                 pool_mu_;
    std::atomic<std::size_t>    rr_{0};
    std::atomic<bool>           stopping_{false};
};

} // namespace shine
