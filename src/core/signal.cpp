#include "core/signal.hpp"
#include "core/logging.hpp"

#include <boost/asio/signal_set.hpp>
#include <csignal>

namespace shine {

awaitable<void> waitForTermination(std::function<awaitable<void>()> onTerminate) {
    auto ex = co_await asio::this_coro::executor;
    asio::signal_set signals(ex, SIGINT, SIGTERM);
    boost::system::error_code ec;
    int sig = co_await signals.async_wait(asio::redirect_error(use_awaitable, ec));
    if (ec) co_return;
    SHINE_INFO("received signal {}, starting graceful shutdown", sig);
    if (onTerminate) {
        co_await onTerminate();
    }
}

} // namespace shine
