#include "outbound/shine_outbound.hpp"

#include "core/logging.hpp"
#include "transport/address.hpp"
#include "transport/pipe.hpp"
#include "transport/tcp_stream.hpp"

#include <boost/asio/connect.hpp>

namespace shine {

awaitable<LinkPtr> ShineOutbound::dialNewLink() {
    auto server = Address::parseHostPort(cfg_.server);
    if (!server.ok()) {
        SHINE_ERROR("bad shine outbound server '{}': {}", cfg_.server, server.status().message());
        co_return nullptr;
    }
    auto ep = co_await server->resolveOne();
    if (!ep.ok()) {
        SHINE_WARN("resolve {} failed: {}", cfg_.server, ep.status().message());
        co_return nullptr;
    }
    tcp::socket sock(ex_);
    boost::system::error_code ec;
    co_await sock.async_connect(*ep, asio::redirect_error(use_awaitable, ec));
    if (ec) {
        SHINE_WARN("shine-outbound connect {} failed: {}", cfg_.server, ec.message());
        co_return nullptr;
    }
    sock.set_option(tcp::no_delay(true), ec);

    LinkOptions lo;
    lo.peer_label        = cfg_.server;
    lo.password          = cfg_.password;
    lo.init_window       = cfg_.init_window;
    lo.ping_interval     = cfg_.ping_interval;
    lo.pong_timeout      = cfg_.pong_timeout;
    lo.handshake_timeout = cfg_.handshake_timeout;
    lo.is_server         = false;

    auto link = std::make_shared<Link>(std::move(sock), std::move(lo));
    // Client links don't accept NEW from peer.
    link->start({});
    co_return link;
}

awaitable<LinkPtr> ShineOutbound::pickOrMakeLink() {
    // Clean closed links, pick next round-robin live one.
    LinkPtr candidate;
    {
        absl::MutexLock lock(&pool_mu_);
        pool_.erase(std::remove_if(pool_.begin(), pool_.end(),
            [](const LinkPtr& l){ return !l || l->isClosed(); }), pool_.end());
        if (!pool_.empty()) {
            auto idx = rr_.fetch_add(1, std::memory_order_relaxed) % pool_.size();
            candidate = pool_[idx];
        }
    }
    if (candidate) co_return candidate;

    // Grow pool up to pool_size.
    auto link = co_await dialNewLink();
    if (!link) co_return nullptr;
    {
        absl::MutexLock lock(&pool_mu_);
        if (pool_.size() < cfg_.pool_size) {
            pool_.push_back(link);
        }
    }
    co_return link;
}

awaitable<Status> ShineOutbound::handle(SessionRequest req) {
    if (stopping_.load(std::memory_order_acquire)) co_return absl::UnavailableError("stopped");
    auto link = co_await pickOrMakeLink();
    if (!link) co_return absl::UnavailableError("no live link");
    bool ok = co_await link->waitHandshake();
    if (!ok) co_return absl::UnavailableError("shine link handshake failed");
    auto s = co_await link->openSession(req.target);
    if (!s.ok()) co_return s.status();
    auto remote_stream = std::static_pointer_cast<ISessionStream>(*s);
    co_await bidiCopy(req.client_stream, remote_stream);
    co_return absl::OkStatus();
}

void ShineOutbound::stop() {
    stopping_.store(true, std::memory_order_release);
    absl::MutexLock lock(&pool_mu_);
    for (auto& l : pool_) if (l) l->closeNow();
    pool_.clear();
}

} // namespace shine
