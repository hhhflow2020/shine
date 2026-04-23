#include "outbound/direct_outbound.hpp"

#include "core/logging.hpp"
#include "transport/pipe.hpp"
#include "transport/tcp_stream.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <absl/container/flat_hash_map.h>
#include <absl/time/time.h>
#include <absl/time/clock.h>

namespace shine {

using boost::asio::ip::udp;

awaitable<Status> DirectOutbound::handle(SessionRequest req) {
    if (req.protocol == SessionRequest::Protocol::UDP) {
        co_return co_await handleUdp(std::move(req));
    }
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

awaitable<Status> DirectOutbound::handleUdp(SessionRequest req) {
    auto client = req.client_datagram_stream;
    if (!client) co_return absl::InvalidArgumentError("no client udp stream");

    udp::socket remote(ex_, udp::v4());
    boost::system::error_code ec;
    remote.bind(udp::endpoint(udp::v4(), 0), ec);
    if (ec) co_return absl::UnavailableError(ec.message());

    absl::flat_hash_map<std::string, std::pair<udp::endpoint, absl::Time>> cache;

    auto read_client = [&]() -> awaitable<void> {
        while (true) {
            auto msg = co_await client->receiveFrom();
            if (!msg.ok()) break;
            
            auto& t = msg->second;
            udp::endpoint dest;
            if (t.isDomain()) {
                auto it = cache.find(t.domain());
                if (it != cache.end() && absl::Now() - it->second.second < absl::Seconds(60)) {
                    dest = it->second.first;
                } else {
                    auto rep = co_await t.resolveOne();
                    if (!rep.ok()) continue;
                    dest = udp::endpoint(rep->address(), rep->port());
                    if (cache.size() < 1024) {
                        cache[t.domain()] = std::make_pair(dest, absl::Now());
                    } else if (it != cache.end()) {
                        it->second = std::make_pair(dest, absl::Now());
                    }
                }
            } else {
                if (t.type() == Address::Type::IPv4) {
                    boost::asio::ip::address_v4::bytes_type b;
                    std::memcpy(b.data(), t.bytes().data(), 4);
                    dest = udp::endpoint(boost::asio::ip::make_address_v4(b), t.port());
                } else {
                    boost::asio::ip::address_v6::bytes_type b;
                    std::memcpy(b.data(), t.bytes().data(), 16);
                    dest = udp::endpoint(boost::asio::ip::make_address_v6(b), t.port());
                }
            }

            boost::system::error_code e;
            co_await remote.async_send_to(asio::buffer(*msg->first), dest, asio::redirect_error(use_awaitable, e));
        }
        boost::system::error_code e2;
        remote.close(e2);
    };

    auto read_remote = [&]() -> awaitable<void> {
        std::array<u8, 65536> buf;
        while (true) {
            udp::endpoint sender;
            boost::system::error_code e;
            std::size_t n = co_await remote.async_receive_from(asio::buffer(buf), sender, asio::redirect_error(use_awaitable, e));
            if (e) break;

            Address src;
            if (sender.address().is_v4()) {
                auto b = sender.address().to_v4().to_bytes();
                src = Address::fromIPv4(b, sender.port());
            } else {
                auto b = sender.address().to_v6().to_bytes();
                src = Address::fromIPv6(b, sender.port());
            }
            
            auto st = co_await client->sendTo(std::span<const u8>(buf.data(), n), std::move(src));
            if (!st.ok()) break;
        }
        client->close();
    };

    using namespace boost::asio::experimental::awaitable_operators;
    co_await (read_client() && read_remote());
    co_return absl::OkStatus();
}

} // namespace shine
