#include "transport/tcp_stream.hpp"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace shine {

awaitable<StatusOr<std::size_t>> TcpSocketStream::read(std::span<u8> buf) {
    boost::system::error_code ec;
    auto n = co_await sock_.async_read_some(
        asio::buffer(buf.data(), buf.size()),
        asio::redirect_error(use_awaitable, ec));
    if (ec == asio::error::eof) co_return std::size_t{0};
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return n;
}

awaitable<Status> TcpSocketStream::write(std::span<const u8> data) {
    boost::system::error_code ec;
    co_await asio::async_write(sock_, asio::buffer(data.data(), data.size()),
                               asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError(ec.message());
    co_return absl::OkStatus();
}

awaitable<void> TcpSocketStream::shutdownWrite() {
    boost::system::error_code ec;
    sock_.shutdown(tcp::socket::shutdown_send, ec);
    co_return;
}

void TcpSocketStream::close() {
    boost::system::error_code ec;
    sock_.close(ec);
}

} // namespace shine
