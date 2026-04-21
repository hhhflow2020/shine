#pragma once

#include "transport/session_stream.hpp"

namespace shine {

class TcpSocketStream final : public ISessionStream {
public:
    explicit TcpSocketStream(tcp::socket sock) : sock_(std::move(sock)) {}

    awaitable<StatusOr<std::size_t>> read(std::span<u8> buf) override;
    awaitable<Status>                write(std::span<const u8> data) override;
    awaitable<void>                  shutdownWrite() override;
    void                             close() override;

    tcp::socket& socket() noexcept { return sock_; }

private:
    tcp::socket sock_;
};

} // namespace shine
