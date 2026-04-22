#pragma once

#include "core/buffer.hpp"
#include "core/common.hpp"
#include "transport/address.hpp"
#include "transport/session_stream.hpp"

#include <absl/synchronization/mutex.h>

#include <boost/asio/experimental/channel.hpp>

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>

namespace shine {

class Link;

// Shine logical session running over a Link. Exposes ISessionStream so the
// upper layer (outbound handlers) treats it as a transparent bytestream.
class Session final
    : public ISessionStream,
      public std::enable_shared_from_this<Session> {
public:
    using Strand = asio::strand<asio::any_io_executor>;

    enum class State : u8 { Init, Open, HalfClosedLocal, HalfClosedRemote, Closed };

    // Default initial window: 256 KiB.
    static constexpr u32 kDefaultInitWindow = 256u * 1024u;

    Session(std::shared_ptr<Link> link, u64 sid, Address target,
            u32 peer_initial_window, u32 our_initial_window);
    ~Session() override;

    // ---- ISessionStream ----
    awaitable<StatusOr<std::size_t>> read(std::span<u8> buf) override;
    awaitable<Status>                write(std::span<const u8> data) override;
    awaitable<void>                  shutdownWrite() override;
    void                             close() override;

    // ---- invoked by Link reader ----
    // Push payload for this session. Moves into per-session queue.
    awaitable<void> onData(std::shared_ptr<std::string> payload);
    void onWindow(u32 delta);
    void onPeerClose();   // CLOSE from peer (half-close remote->us)
    void onReset();       // RESET: both sides dead

    u64            sid()     const noexcept { return sid_; }
    State          state()   const noexcept { return state_.load(std::memory_order_acquire); }
    const Address& target()  const noexcept { return target_; }
    std::atomic<u64>& bytesIn()  noexcept { return bytes_in_; }
    std::atomic<u64>& bytesOut() noexcept { return bytes_out_; }

private:
    // Sends a WINDOW frame to peer if recv_unacked_ >= half window.
    awaitable<void> maybeSendWindow();

    std::weak_ptr<Link> link_;
    u64                 sid_;
    Address             target_;

    // Flow control.
    u32 our_initial_window_;                 // window we advertised
    std::atomic<i64> send_credit_{0};        // remaining bytes we may send
    std::atomic<u32> recv_unacked_{0};       // bytes consumed, pending WINDOW-back

    // State.
    std::atomic<State> state_{State::Init};

    // Inbound payload queue (from Link reader -> read()).
    using PayloadChan = asio::experimental::channel<void(boost::system::error_code,
                                                         std::shared_ptr<std::string>)>;
    std::unique_ptr<PayloadChan> inbox_;

    // Leftover bytes from a payload that didn't fit in the last read() buffer.
    std::shared_ptr<std::string> leftover_;
    std::size_t                  leftover_off_ = 0;

    // Credit wake-up channel: writer blocks here until WINDOW frames arrive.
    using CreditChan = asio::experimental::channel<void(boost::system::error_code)>;
    std::unique_ptr<CreditChan> credit_signal_;

    std::atomic<u64> bytes_in_{0};
    std::atomic<u64> bytes_out_{0};
};

using SessionPtr = std::shared_ptr<Session>;

} // namespace shine
