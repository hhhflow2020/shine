#pragma once

#include "config/schema.hpp"
#include "inbound/inbound.hpp"

#include <atomic>
#include <memory>

namespace shine {

class Socks5Inbound final : public IInbound,
                            public std::enable_shared_from_this<Socks5Inbound> {
public:
    Socks5Inbound(asio::any_io_executor ex, config::InboundConfig cfg)
        : ex_(std::move(ex)), cfg_(std::move(cfg)), acceptor_(ex_) {}

    Status start(SessionRequestHandler on_req) override;
    void   stop() override;

    const std::string& tag() const override { return cfg_.tag; }
    const std::string& protocol() const override { return cfg_.protocol; }

private:
    awaitable<void> acceptLoop();
    awaitable<void> handleClient(tcp::socket sock);

    asio::any_io_executor     ex_;
    config::InboundConfig     cfg_;
    tcp::acceptor             acceptor_;
    SessionRequestHandler     on_req_;
    std::atomic<bool>         stopping_{false};
};

} // namespace shine
