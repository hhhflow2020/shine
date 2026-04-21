#pragma once

#include "core/common.hpp"

#include <thread>
#include <vector>

namespace shine {

class Scheduler {
public:
    explicit Scheduler(std::size_t io_threads = 0);
    ~Scheduler();

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void run();   // start worker threads, non-blocking
    void stop();  // graceful: stop work_guard, join threads
    void join();

    asio::io_context&     io()      { return io_; }
    asio::any_io_executor executor(){ return io_.get_executor(); }

private:
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> guard_;
    std::size_t n_threads_;
    std::vector<std::thread> threads_;
    bool stopped_ = false;
};

} // namespace shine
