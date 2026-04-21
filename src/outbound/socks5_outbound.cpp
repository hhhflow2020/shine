#include "outbound/socks5_outbound.hpp"

#include "core/logging.hpp"
#include "transport/address.hpp"
#include "transport/pipe.hpp"
#include "transport/tcp_stream.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <cstring>

namespace shine {

namespace {

awaitable<Status> s5WriteAll(tcp::socket& s, const u8* p, std::size_t n) {
    boost::system::error_code ec;
    co_await asio::async_write(s, asio::buffer(p, n),
                               asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return absl::OkStatus();
}
awaitable<Status> s5ReadExact(tcp::socket& s, u8* p, std::size_t n) {
    boost::system::error_code ec;
    co_await asio::async_read(s, asio::buffer(p, n),
                              asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return absl::OkStatus();
}

} // namespace

awaitable<Status> Socks5Outbound::handle(SessionRequest req) {
    auto server = Address::parseHostPort(cfg_.server);
    if (!server.ok()) co_return server.status();
    auto ep = co_await server->resolveOne();
    if (!ep.ok()) co_return ep.status();
    tcp::socket remote(ex_);
    boost::system::error_code ec;
    co_await remote.async_connect(*ep, asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());

    const bool use_auth = !cfg_.user.empty();

    // Method select
    u8 greet[4] = {0x05, 0x02, 0x00, 0x02};   // offer no-auth and userpass
    std::size_t glen = use_auth ? 4 : 3;
    if (auto s = co_await s5WriteAll(remote, greet, glen); !s.ok()) co_return s;
    u8 reply[2];
    if (auto s = co_await s5ReadExact(remote, reply, 2); !s.ok()) co_return s;
    if (reply[0] != 0x05) co_return absl::UnavailableError("bad socks5 ver");
    if (reply[1] == 0xFF) co_return absl::UnavailableError("no acceptable method");
    if (reply[1] == 0x02) {
        std::vector<u8> buf;
        buf.push_back(0x01);
        buf.push_back(static_cast<u8>(cfg_.user.size()));
        buf.insert(buf.end(), cfg_.user.begin(), cfg_.user.end());
        buf.push_back(static_cast<u8>(cfg_.pass.size()));
        buf.insert(buf.end(), cfg_.pass.begin(), cfg_.pass.end());
        if (auto s = co_await s5WriteAll(remote, buf.data(), buf.size()); !s.ok()) co_return s;
        u8 ar[2];
        if (auto s = co_await s5ReadExact(remote, ar, 2); !s.ok()) co_return s;
        if (ar[1] != 0) co_return absl::UnauthenticatedError("socks5 auth failed");
    }

    // Request CONNECT
    std::vector<u8> r;
    r.push_back(0x05);
    r.push_back(0x01);
    r.push_back(0x00);
    switch (req.target.type()) {
        case Address::Type::IPv4:
            r.push_back(0x01);
            r.insert(r.end(), req.target.bytes().begin(), req.target.bytes().begin() + 4);
            break;
        case Address::Type::IPv6:
            r.push_back(0x04);
            r.insert(r.end(), req.target.bytes().begin(), req.target.bytes().end());
            break;
        case Address::Type::Domain:
            r.push_back(0x03);
            r.push_back(static_cast<u8>(std::min<std::size_t>(255, req.target.domain().size())));
            r.insert(r.end(), req.target.domain().begin(), req.target.domain().end());
            break;
    }
    u16 port = req.target.port();
    r.push_back(static_cast<u8>(port >> 8));
    r.push_back(static_cast<u8>(port & 0xff));
    if (auto s = co_await s5WriteAll(remote, r.data(), r.size()); !s.ok()) co_return s;

    u8 rep[4];
    if (auto s = co_await s5ReadExact(remote, rep, 4); !s.ok()) co_return s;
    if (rep[1] != 0) co_return absl::UnavailableError("socks5 CONNECT rejected");
    std::size_t extra = 0;
    switch (rep[3]) {
        case 0x01: extra = 4; break;
        case 0x04: extra = 16; break;
        case 0x03: {
            u8 dl;
            if (auto s = co_await s5ReadExact(remote, &dl, 1); !s.ok()) co_return s;
            extra = dl;
        } break;
        default: co_return absl::UnavailableError("bad socks5 atyp");
    }
    std::vector<u8> skip(extra + 2);
    if (auto s = co_await s5ReadExact(remote, skip.data(), skip.size()); !s.ok()) co_return s;

    auto remote_stream = std::make_shared<TcpSocketStream>(std::move(remote));
    co_await bidiCopy(req.client_stream, remote_stream);
    co_return absl::OkStatus();
}

} // namespace shine
