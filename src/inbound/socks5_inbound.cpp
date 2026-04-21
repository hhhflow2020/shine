#include "inbound/socks5_inbound.hpp"

#include "core/logging.hpp"
#include "transport/address.hpp"
#include "transport/tcp_stream.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <cstring>

namespace shine {

Status Socks5Inbound::start(SessionRequestHandler on_req) {
    on_req_ = std::move(on_req);
    auto ep = Address::parseHostPort(cfg_.listen);
    if (!ep.ok()) return ep.status();
    boost::system::error_code ec;
    tcp::endpoint real;
    if (ep->type() == Address::Type::IPv4) {
        boost::asio::ip::address_v4::bytes_type b;
        std::memcpy(b.data(), ep->bytes().data(), 4);
        real = tcp::endpoint(boost::asio::ip::make_address_v4(b), ep->port());
    } else if (ep->type() == Address::Type::IPv6) {
        boost::asio::ip::address_v6::bytes_type b;
        std::memcpy(b.data(), ep->bytes().data(), 16);
        real = tcp::endpoint(boost::asio::ip::make_address_v6(b), ep->port());
    } else {
        return absl::InvalidArgumentError("socks5 listen must be IP");
    }
    acceptor_.open(real.protocol(), ec);
    if (ec) return absl::UnavailableError(ec.message());
    acceptor_.set_option(tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(real, ec);
    if (ec) return absl::UnavailableError(ec.message());
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec) return absl::UnavailableError(ec.message());

    auto self = shared_from_this();
    asio::co_spawn(ex_, [self]() { return self->acceptLoop(); }, asio::detached);
    SHINE_INFO("socks5 inbound '{}' listening on {}", cfg_.tag, cfg_.listen);
    return absl::OkStatus();
}

void Socks5Inbound::stop() {
    stopping_.store(true, std::memory_order_release);
    boost::system::error_code ec;
    acceptor_.close(ec);
}

awaitable<void> Socks5Inbound::acceptLoop() {
    while (!stopping_.load(std::memory_order_acquire)) {
        boost::system::error_code ec;
        auto sock = co_await acceptor_.async_accept(asio::redirect_error(use_awaitable, ec));
        if (ec) {
            if (!stopping_) SHINE_WARN("socks5 accept error: {}", ec.message());
            co_return;
        }
        auto self = shared_from_this();
        asio::co_spawn(ex_,
            [self, s = std::move(sock)]() mutable {
                return self->handleClient(std::move(s));
            },
            asio::detached);
    }
}

namespace {

awaitable<Status> readExact(tcp::socket& s, u8* buf, std::size_t n) {
    boost::system::error_code ec;
    co_await asio::async_read(s, asio::buffer(buf, n),
                              asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return absl::OkStatus();
}

awaitable<Status> writeAll(tcp::socket& s, const u8* buf, std::size_t n) {
    boost::system::error_code ec;
    co_await asio::async_write(s, asio::buffer(buf, n),
                               asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return absl::OkStatus();
}

} // namespace

awaitable<void> Socks5Inbound::handleClient(tcp::socket sock) {
    // --- method select ---
    u8 hdr[2];
    if (!(co_await readExact(sock, hdr, 2)).ok()) co_return;
    if (hdr[0] != 0x05) co_return;
    u8 nm = hdr[1];
    if (nm == 0) co_return;
    std::vector<u8> methods(nm);
    if (!(co_await readExact(sock, methods.data(), nm)).ok()) co_return;

    const bool require_auth = !cfg_.user.empty() || !cfg_.pass.empty();
    u8 chosen = 0xFF;
    for (u8 m : methods) {
        if (!require_auth && m == 0x00) { chosen = 0x00; break; }
        if (require_auth  && m == 0x02) { chosen = 0x02; break; }
    }
    {
        u8 resp[2] = {0x05, chosen};
        if (!(co_await writeAll(sock, resp, 2)).ok()) co_return;
        if (chosen == 0xFF) co_return;
    }

    if (require_auth) {
        // RFC 1929
        u8 v; if (!(co_await readExact(sock, &v, 1)).ok()) co_return;
        if (v != 0x01) co_return;
        u8 ulen; if (!(co_await readExact(sock, &ulen, 1)).ok()) co_return;
        std::string u(ulen, '\0');
        if (ulen && !(co_await readExact(sock, reinterpret_cast<u8*>(u.data()), ulen)).ok()) co_return;
        u8 plen; if (!(co_await readExact(sock, &plen, 1)).ok()) co_return;
        std::string p(plen, '\0');
        if (plen && !(co_await readExact(sock, reinterpret_cast<u8*>(p.data()), plen)).ok()) co_return;
        u8 status = (u == cfg_.user && p == cfg_.pass) ? 0x00 : 0x01;
        u8 resp[2] = {0x01, status};
        if (!(co_await writeAll(sock, resp, 2)).ok()) co_return;
        if (status != 0) co_return;
    }

    // --- request ---
    u8 req_hdr[4];
    if (!(co_await readExact(sock, req_hdr, 4)).ok()) co_return;
    if (req_hdr[0] != 0x05 || req_hdr[1] != 0x01) {  // only CONNECT
        u8 rep[10] = {0x05, 0x07, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        (void)co_await writeAll(sock, rep, 10);
        co_return;
    }
    Address target;
    switch (req_hdr[3]) {
        case 0x01: {
            u8 buf[6];
            if (!(co_await readExact(sock, buf, 6)).ok()) co_return;
            std::array<u8, 4> b{buf[0], buf[1], buf[2], buf[3]};
            u16 port = (static_cast<u16>(buf[4]) << 8) | buf[5];
            target = Address::fromIPv4(b, port);
        } break;
        case 0x03: {
            u8 dlen; if (!(co_await readExact(sock, &dlen, 1)).ok()) co_return;
            std::string d(dlen, '\0');
            if (dlen && !(co_await readExact(sock, reinterpret_cast<u8*>(d.data()), dlen)).ok()) co_return;
            u8 pb[2];
            if (!(co_await readExact(sock, pb, 2)).ok()) co_return;
            u16 port = (static_cast<u16>(pb[0]) << 8) | pb[1];
            target = Address::fromDomain(std::move(d), port);
        } break;
        case 0x04: {
            u8 buf[18];
            if (!(co_await readExact(sock, buf, 18)).ok()) co_return;
            std::array<u8, 16> b;
            std::memcpy(b.data(), buf, 16);
            u16 port = (static_cast<u16>(buf[16]) << 8) | buf[17];
            target = Address::fromIPv6(b, port);
        } break;
        default:
            co_return;
    }

    // Reply "succeeded" with local-bound dummy (0.0.0.0:0) — most clients accept it.
    u8 rep[10] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    if (!(co_await writeAll(sock, rep, 10)).ok()) co_return;

    SessionRequest r;
    r.inbound_tag      = cfg_.tag;
    r.inbound_protocol = cfg_.protocol;
    r.target           = std::move(target);
    r.client_stream    = std::make_shared<TcpSocketStream>(std::move(sock));
    if (on_req_) co_await on_req_(std::move(r));
}

} // namespace shine
