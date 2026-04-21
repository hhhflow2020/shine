#include "outbound/http_connect_outbound.hpp"

#include "core/logging.hpp"
#include "transport/address.hpp"
#include "transport/pipe.hpp"
#include "transport/tcp_stream.hpp"

#include <absl/strings/str_cat.h>
#include <absl/strings/escaping.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>

namespace shine {

awaitable<Status> HttpConnectOutbound::handle(SessionRequest req) {
    auto server = Address::parseHostPort(cfg_.server);
    if (!server.ok()) co_return server.status();
    auto ep = co_await server->resolveOne();
    if (!ep.ok()) co_return ep.status();
    tcp::socket remote(ex_);
    boost::system::error_code ec;
    co_await remote.async_connect(*ep, asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());

    std::string host_port;
    if (req.target.type() == Address::Type::IPv6) {
        host_port = "[" + req.target.toString() + "]";
        host_port = req.target.toString();   // already bracketed for v6
    } else {
        host_port = req.target.toString();
    }

    std::string reqline = absl::StrCat("CONNECT ", host_port, " HTTP/1.1\r\nHost: ", host_port, "\r\n");
    if (!cfg_.user.empty()) {
        std::string cred = absl::StrCat(cfg_.user, ":", cfg_.pass);
        std::string b64;
        absl::Base64Escape(cred, &b64);
        absl::StrAppend(&reqline, "Proxy-Authorization: Basic ", b64, "\r\n");
    }
    reqline += "\r\n";

    co_await asio::async_write(remote, asio::buffer(reqline),
                               asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());

    asio::streambuf sb;
    co_await asio::async_read_until(remote, sb, "\r\n\r\n",
                                    asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    std::istream is(&sb);
    std::string status;
    std::getline(is, status);
    if (status.find(" 200 ") == std::string::npos) {
        co_return absl::UnavailableError("http connect failed: " + status);
    }
    auto remote_stream = std::make_shared<TcpSocketStream>(std::move(remote));
    co_await bidiCopy(req.client_stream, remote_stream);
    co_return absl::OkStatus();
}

} // namespace shine
