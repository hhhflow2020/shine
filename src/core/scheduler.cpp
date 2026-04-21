#include "core/scheduler.hpp"
#include "core/logging.hpp"

namespace shine {

Scheduler::Scheduler(std::size_t io_threads)
    : io_(), guard_(asio::make_work_guard(io_)),
      n_threads_(io_threads == 0 ? std::max<std::size_t>(1, std::thread::hardware_concurrency()) : io_threads) {}

Scheduler::~Scheduler() {
    stop();
    join();
}

void Scheduler::run() {
    threads_.reserve(n_threads_);
    for (std::size_t i = 0; i < n_threads_; ++i) {
        threads_.emplace_back([this, i]() {
            SHINE_DEBUG("io worker {} started", i);
            try {
                io_.run();
            } catch (const std::exception& e) {
                SHINE_ERROR("io worker {} exception: {}", i, e.what());
            }
            SHINE_DEBUG("io worker {} exited", i);
        });
    }
    SHINE_INFO("scheduler running with {} threads", n_threads_);
}

void Scheduler::stop() {
    if (stopped_) return;
    stopped_ = true;
    guard_.reset();
    io_.stop();
}

void Scheduler::join() {
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

} // namespace shine
