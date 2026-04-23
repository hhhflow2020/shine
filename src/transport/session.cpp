#include "transport/session.hpp"

#include "core/logging.hpp"
#include "proto/shine_frame.hpp"
#include "transport/link.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace shine {

Session::Session(std::shared_ptr<Link> link, u64 sid, Address target,
                 u32 peer_initial_window, u32 our_initial_window,
                 SessionRequest::Protocol protocol)
    : link_(link), sid_(sid), target_(std::move(target)),
      our_initial_window_(our_initial_window), protocol_(protocol) {
    send_credit_.store(static_cast<i64>(peer_initial_window), std::memory_order_relaxed);

    auto ex = link->executor();
    if (protocol == SessionRequest::Protocol::TCP) {
        inbox_         = std::make_unique<PayloadChan>(ex, 1024);
        credit_signal_ = std::make_unique<CreditChan>(ex, 1);
    } else {
        udp_inbox_ = std::make_unique<UdpPayloadChan>(ex, 1024);
    }
    state_.store(State::Open, std::memory_order_relaxed);
}

Session::~Session() = default;

awaitable<StatusOr<std::size_t>> Session::read(std::span<u8> buf) {
    if (leftover_ && leftover_off_ < leftover_->size()) {
        std::size_t n = std::min(buf.size(), leftover_->size() - leftover_off_);
        std::memcpy(buf.data(), leftover_->data() + leftover_off_, n);
        leftover_off_ += n;
        if (leftover_off_ >= leftover_->size()) {
            leftover_.reset();
            leftover_off_ = 0;
        }
        bytes_in_.fetch_add(n, std::memory_order_relaxed);
        recv_unacked_.fetch_add(static_cast<u32>(n), std::memory_order_relaxed);
        co_await maybeSendWindow();
        co_return n;
    }
    auto st = state_.load(std::memory_order_acquire);
    if (st == State::Closed || st == State::HalfClosedRemote) {
        co_return std::size_t{0};
    }
    boost::system::error_code ec;
    auto pay = co_await inbox_->async_receive(asio::redirect_error(use_awaitable, ec));
    if (ec) co_return std::size_t{0};
    if (!pay) co_return std::size_t{0};
    std::size_t n = std::min(buf.size(), pay->size());
    std::memcpy(buf.data(), pay->data(), n);
    if (n < pay->size()) {
        leftover_     = std::move(pay);
        leftover_off_ = n;
    }
    bytes_in_.fetch_add(n, std::memory_order_relaxed);
    recv_unacked_.fetch_add(static_cast<u32>(n), std::memory_order_relaxed);
    co_await maybeSendWindow();
    co_return n;
}

awaitable<Status> Session::write(std::span<const u8> data) {
    auto link = link_.lock();
    if (!link || link->isClosed()) co_return absl::UnavailableError("link closed");
    auto st = state_.load(std::memory_order_acquire);
    if (st == State::Closed || st == State::HalfClosedLocal) {
        co_return absl::FailedPreconditionError("session write side closed");
    }

    std::size_t remaining = data.size();
    const u8*   p = data.data();
    while (remaining > 0) {
        while (send_credit_.load(std::memory_order_acquire) <= 0) {
            boost::system::error_code ec;
            co_await credit_signal_->async_receive(asio::redirect_error(use_awaitable, ec));
            if (link->isClosed()) co_return absl::UnavailableError("link closed");
        }
        std::size_t chunk = std::min<std::size_t>({
            remaining,
            link->options().max_data_frame,
            static_cast<std::size_t>(send_credit_.load(std::memory_order_acquire))
        });
        if (chunk == 0) continue;
        std::string wire;
        wire.reserve(chunk + 32);
        proto::encodeData(wire, sid_, p, chunk);
        send_credit_.fetch_sub(static_cast<i64>(chunk), std::memory_order_acq_rel);
        co_await link->enqueueFrame(std::move(wire));
        bytes_out_.fetch_add(chunk, std::memory_order_relaxed);
        remaining -= chunk;
        p         += chunk;
    }
    co_return absl::OkStatus();
}

awaitable<void> Session::shutdownWrite() {
    auto link = link_.lock();
    if (!link || link->isClosed()) co_return;
    auto expected = State::Open;
    if (!state_.compare_exchange_strong(expected, State::HalfClosedLocal)) {
        if (expected == State::HalfClosedRemote) {
            state_.store(State::Closed, std::memory_order_release);
            link->eraseSession(sid_);
        } else {
            co_return;
        }
    }
    std::string wire;
    proto::encodeClose(wire, proto::CloseFrame{sid_, ""});
    co_await link->enqueueFrame(std::move(wire));
}

void Session::close() {
    auto expected = state_.load(std::memory_order_acquire);
    if (expected != State::Closed) {
        state_.store(State::Closed, std::memory_order_release);
        if (auto lk = link_.lock()) {
            if (!lk->isClosed()) {
                std::string wire;
                proto::encodeClose(wire, proto::CloseFrame{sid_, ""});
                asio::co_spawn(lk->executor(), lk->enqueueFrame(std::move(wire)), asio::detached);
            }
            lk->eraseSession(sid_);
        }
    }
    if (inbox_) inbox_->close();
    if (udp_inbox_) udp_inbox_->close();
    if (credit_signal_) credit_signal_->close();
}

awaitable<Status> Session::sendTo(std::span<const u8> data, class Address target) {
    auto link = link_.lock();
    if (!link || link->isClosed()) co_return absl::UnavailableError("link closed");
    auto st = state_.load(std::memory_order_acquire);
    if (st == State::Closed || st == State::HalfClosedLocal) {
        co_return absl::FailedPreconditionError("session write side closed");
    }

    std::string wire;
    // UDP_DATA overhead: array format, roughly 1+8+N bytes depending on target length
    proto::encodeUdpData(wire, proto::UdpDataFrame{sid_, target.toBinary(), std::string(reinterpret_cast<const char*>(data.data()), data.size())});
    co_await link->enqueueFrame(std::move(wire));
    bytes_out_.fetch_add(data.size(), std::memory_order_relaxed);
    co_return absl::OkStatus();
}

awaitable<StatusOr<std::pair<std::shared_ptr<std::string>, class Address>>> Session::receiveFrom() {
    auto st = state_.load(std::memory_order_acquire);
    if (st == State::Closed || st == State::HalfClosedRemote) {
        co_return absl::UnavailableError("session closed");
    }
    boost::system::error_code ec;
    auto pay = co_await udp_inbox_->async_receive(asio::redirect_error(use_awaitable, ec));
    if (ec) co_return absl::UnavailableError("session closed");
    bytes_in_.fetch_add(pay.first->size(), std::memory_order_relaxed);
    co_return pay;
}

awaitable<void> Session::onData(std::shared_ptr<std::string> payload) {
    if (!payload || payload->empty() || !inbox_) co_return;
    boost::system::error_code ec;
    co_await inbox_->async_send(boost::system::error_code{}, std::move(payload),
                                asio::redirect_error(use_awaitable, ec));
}

awaitable<void> Session::onUdpData(std::shared_ptr<std::string> payload, class Address addr) {
    if (!payload || !udp_inbox_) co_return;
    // Use try_send to naturally drop UDP packets if the queue is full.
    // This prevents a single congested UDP session from blocking the multiplexer's read loop.
    udp_inbox_->try_send(boost::system::error_code{}, std::make_pair(std::move(payload), std::move(addr)));
    co_return;
}

void Session::onWindow(u32 delta) {
    send_credit_.fetch_add(static_cast<i64>(delta), std::memory_order_acq_rel);
    if (credit_signal_) credit_signal_->try_send(boost::system::error_code{});
}

void Session::onPeerClose() {
    auto s = state_.load(std::memory_order_acquire);
    if (s == State::HalfClosedLocal) {
        state_.store(State::Closed, std::memory_order_release);
    } else if (s == State::Open) {
        state_.store(State::HalfClosedRemote, std::memory_order_release);
    }
    if (inbox_) inbox_->close();
    if (udp_inbox_) udp_inbox_->close();
}

void Session::onReset() {
    state_.store(State::Closed, std::memory_order_release);
    if (inbox_) inbox_->close();
    if (udp_inbox_) udp_inbox_->close();
    if (credit_signal_) credit_signal_->close();
}

awaitable<void> Session::maybeSendWindow() {
    if (auto link = link_.lock(); link && !link->isClosed()) {
        u32 threshold = our_initial_window_ / 2;
        u32 cur       = recv_unacked_.load(std::memory_order_relaxed);
        if (cur >= threshold) {
            u32 delta = recv_unacked_.exchange(0, std::memory_order_acq_rel);
            std::string wire;
            proto::encodeWindow(wire, proto::WindowFrame{sid_, delta});
            co_await link->enqueueFrame(std::move(wire));
        }
    }
    co_return;
}

} // namespace shine
