#include "transport/pipe.hpp"
#include "core/logging.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <array>

namespace shine {

using namespace boost::asio::experimental::awaitable_operators;

namespace {

awaitable<void> halfCopy(std::shared_ptr<ISessionStream> src,
                         std::shared_ptr<ISessionStream> dst) {
    std::array<u8, 32 * 1024> buf;
    for (;;) {
        auto r = co_await src->read(std::span<u8>(buf.data(), buf.size()));
        if (!r.ok()) break;
        if (*r == 0) break;
        auto w = co_await dst->write(std::span<const u8>(buf.data(), *r));
        if (!w.ok()) break;
    }
    co_await dst->shutdownWrite();
}

} // namespace

awaitable<void> bidiCopy(std::shared_ptr<ISessionStream> a,
                         std::shared_ptr<ISessionStream> b) {
    co_await (halfCopy(a, b) && halfCopy(b, a));
    a->close();
    b->close();
}

} // namespace shine
