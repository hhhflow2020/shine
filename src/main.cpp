#include "config/yaml_loader.hpp"
#include "core/logging.hpp"
#include "core/scheduler.hpp"
#include "core/signal.hpp"
#include "inbound/factory.hpp"
#include "metrics/exporter.hpp"
#include "metrics/registry.hpp"
#include "outbound/factory.hpp"
#include "route/router.hpp"

#include <absl/container/flat_hash_map.h>

#include <boost/asio/co_spawn.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace shine;

namespace {

void usage() {
    std::cerr << "usage: shine -c <config.yaml>\n";
}

awaitable<void> dispatch(SessionRequest req,
                         std::shared_ptr<Router> router,
                         std::shared_ptr<absl::flat_hash_map<std::string, OutboundPtr>> obs) {
    absl::string_view tag = router->pick(req);
    if (tag.empty()) {
        SHINE_WARN("no route matched & no default; dropping target={}", req.target.toString());
        co_return;
    }
    auto it = obs->find(std::string(tag));
    if (it == obs->end()) {
        SHINE_WARN("route outbound '{}' not found; dropping", tag);
        co_return;
    }
    metrics::instance().sessions_opened
        .Add({{"inbound", req.inbound_tag}, {"outbound", std::string(tag)}})
        .Increment();
    auto& sa = metrics::instance().sessions_active
        .Add({{"inbound", req.inbound_tag}, {"outbound", std::string(tag)}});
    sa.Increment();
    auto st = co_await it->second->handle(std::move(req));
    sa.Decrement();
    if (!st.ok()) {
        metrics::instance().errors_total.Add({{"kind", "outbound"}}).Increment();
        SHINE_DEBUG("outbound '{}' handle: {}", tag, st.message());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string cfg_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" && i + 1 < argc) { cfg_path = argv[++i]; }
        else if (a == "-h" || a == "--help") { usage(); return 0; }
    }
    if (cfg_path.empty()) { usage(); return 1; }

    auto cfg_or = config::loadFromFile(cfg_path);
    if (!cfg_or.ok()) {
        std::cerr << "config error: " << cfg_or.status().message() << "\n";
        return 1;
    }
    const auto& cfg = *cfg_or;

    log::init(cfg.log.level);
    SHINE_INFO("shine starting, config={}", cfg_path);

    Scheduler sched(cfg.server.io_threads);
    sched.run();
    auto ex = sched.executor();

    // Metrics.
    metrics::Exporter exporter;
    if (auto s = exporter.start(cfg.metrics); !s.ok()) {
        SHINE_ERROR("metrics start failed: {}", s.message());
    }

    // Outbounds.
    auto outbounds = std::make_shared<absl::flat_hash_map<std::string, OutboundPtr>>();
    for (const auto& oc : cfg.outbounds) {
        auto ob = makeOutbound(ex, oc);
        if (!ob.ok()) {
            SHINE_ERROR("build outbound '{}' failed: {}", oc.tag, ob.status().message());
            return 2;
        }
        outbounds->emplace(oc.tag, *ob);
    }

    // Router.
    auto router_or = Router::compile(cfg.route);
    if (!router_or.ok()) {
        SHINE_ERROR("route compile: {}", router_or.status().message());
        return 2;
    }
    auto router = std::make_shared<Router>(std::move(*router_or));

    // Inbounds.
    std::vector<InboundPtr> inbounds;
    for (const auto& ic : cfg.inbounds) {
        auto ib = makeInbound(ex, ic);
        if (!ib.ok()) {
            SHINE_ERROR("build inbound '{}' failed: {}", ic.tag, ib.status().message());
            return 2;
        }
        auto start_st = (*ib)->start(
            [router, outbounds](SessionRequest req) -> awaitable<void> {
                co_await dispatch(std::move(req), router, outbounds);
            });
        if (!start_st.ok()) {
            SHINE_ERROR("start inbound '{}' failed: {}", ic.tag, start_st.message());
            return 2;
        }
        inbounds.push_back(*ib);
    }

    asio::co_spawn(sched.io(),
        [&]() -> awaitable<void> {
            co_await waitForTermination([&]() -> awaitable<void> {
                SHINE_INFO("graceful shutdown: stopping inbounds");
                for (auto& ib : inbounds) ib->stop();
                // Give outbounds a window to drain.
                asio::steady_timer t(co_await asio::this_coro::executor);
                t.expires_after(cfg.server.graceful_timeout);
                boost::system::error_code ec;
                co_await t.async_wait(asio::redirect_error(use_awaitable, ec));
                for (auto& kv : *outbounds) kv.second->stop();
                exporter.stop();
                // Stop scheduler.
                boost::asio::post(co_await asio::this_coro::executor, []{});
                co_return;
            });
            // After graceful handler returns, stop the io_context.
        },
        asio::detached);

    // Wait until io_context has no work — signal handler will stop the scheduler.
    asio::co_spawn(sched.io(),
        [&]() -> awaitable<void> {
            asio::steady_timer t(co_await asio::this_coro::executor);
            t.expires_after(std::chrono::hours(24 * 365));
            boost::system::error_code ec;
            co_await t.async_wait(asio::redirect_error(use_awaitable, ec));
        },
        asio::detached);

    // Main thread waits on signal directly.
    {
        asio::io_context main_ctx;
        asio::signal_set sigs(main_ctx, SIGINT, SIGTERM);
        sigs.async_wait([&](const boost::system::error_code&, int sig) {
            SHINE_INFO("main: got signal {}, shutting down", sig);
            for (auto& ib : inbounds) ib->stop();
            for (auto& kv : *outbounds) kv.second->stop();
            exporter.stop();
            sched.stop();
        });
        main_ctx.run();
    }
    sched.join();
    SHINE_INFO("shine exited cleanly");
    return 0;
}
