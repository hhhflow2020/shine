#include "outbound/direct_outbound.hpp"

#include "core/logging.hpp"
#include "transport/pipe.hpp"
#include "transport/tcp_stream.hpp"

#include <boost/asio/connect.hpp>

namespace shine {

awaitable<Status> DirectOutbound::handle(SessionRequest req) {
    auto ep = co_await req.target.resolveOne();
    if (!ep.ok()) co_return ep.status();
    tcp::socket remote(ex_);
    boost::system::error_code ec;
    co_await remote.async_connect(*ep, asio::redirect_error(use_awaitable, ec));
    if (ec) {
        SHINE_DEBUG("direct connect {} failed: {}", req.target.toString(), ec.message());
        co_return absl::UnavailableError(ec.message());
    }
    boost::system::error_code e2;
    remote.set_option(tcp::no_delay(true), e2);
    auto remote_stream = std::make_shared<TcpSocketStream>(std::move(remote));
    co_await bidiCopy(req.client_stream, remote_stream);
    co_return absl::OkStatus();
}

} // namespace shine
