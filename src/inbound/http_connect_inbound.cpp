#include "inbound/http_connect_inbound.hpp"

#include "core/logging.hpp"
#include "transport/tcp_stream.hpp"

#include <absl/strings/str_split.h>
#include <absl/strings/numbers.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

#include <cstring>

namespace shine {

Status HttpConnectInbound::start(SessionRequestHandler on_req) {
    on_req_ = std::move(on_req);
    auto ep = Address::parseHostPort(cfg_.listen);
    if (!ep.ok()) return ep.status();
    if (ep->type() == Address::Type::Domain)
        return absl::InvalidArgumentError("http-connect listen must be IP");
    boost::system::error_code ec;
    tcp::endpoint real;
    if (ep->type() == Address::Type::IPv4) {
        boost::asio::ip::address_v4::bytes_type b;
        std::memcpy(b.data(), ep->bytes().data(), 4);
        real = tcp::endpoint(boost::asio::ip::make_address_v4(b), ep->port());
    } else {
        boost::asio::ip::address_v6::bytes_type b;
        std::memcpy(b.data(), ep->bytes().data(), 16);
        real = tcp::endpoint(boost::asio::ip::make_address_v6(b), ep->port());
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
    SHINE_INFO("http-connect inbound '{}' listening on {}", cfg_.tag, cfg_.listen);
    return absl::OkStatus();
}

void HttpConnectInbound::stop() {
    stopping_.store(true, std::memory_order_release);
    boost::system::error_code ec;
    acceptor_.close(ec);
}

awaitable<void> HttpConnectInbound::acceptLoop() {
    while (!stopping_.load(std::memory_order_acquire)) {
        boost::system::error_code ec;
        auto sock = co_await acceptor_.async_accept(asio::redirect_error(use_awaitable, ec));
        if (ec) { if (!stopping_) SHINE_WARN("http accept err: {}", ec.message()); co_return; }
        auto self = shared_from_this();
        asio::co_spawn(ex_,
            [self, s = std::move(sock)]() mutable {
                return self->handleClient(std::move(s));
            },
            asio::detached);
    }
}

awaitable<void> HttpConnectInbound::handleClient(tcp::socket sock) {
    boost::system::error_code ec;
    asio::streambuf sb;
    co_await asio::async_read_until(sock, sb, "\r\n\r\n",
                                    asio::redirect_error(use_awaitable, ec));
    if (ec) co_return;
    std::istream is(&sb);
    std::string request_line;
    std::getline(is, request_line);
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();

    // Expect: "CONNECT host:port HTTP/1.1"
    auto parts = absl::StrSplit(request_line, ' ');
    std::vector<std::string> tok(parts.begin(), parts.end());
    if (tok.size() < 2 || tok[0] != "CONNECT") {
        static const char* r = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        co_await asio::async_write(sock, asio::buffer(r, std::strlen(r)),
                                   asio::redirect_error(use_awaitable, ec));
        co_return;
    }
    auto target = Address::parseHostPort(tok[1]);
    if (!target.ok()) {
        static const char* r = "HTTP/1.1 400 Bad Request\r\n\r\n";
        co_await asio::async_write(sock, asio::buffer(r, std::strlen(r)),
                                   asio::redirect_error(use_awaitable, ec));
        co_return;
    }
    static const char* ok = "HTTP/1.1 200 Connection Established\r\n\r\n";
    co_await asio::async_write(sock, asio::buffer(ok, std::strlen(ok)),
                               asio::redirect_error(use_awaitable, ec));
    if (ec) co_return;

    SessionRequest r;
    r.inbound_tag      = cfg_.tag;
    r.inbound_protocol = cfg_.protocol;
    r.target           = *target;
    r.client_stream    = std::make_shared<TcpSocketStream>(std::move(sock));
    if (on_req_) co_await on_req_(std::move(r));
}

} // namespace shine
