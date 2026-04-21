#pragma once

#include "core/buffer.hpp"
#include "core/common.hpp"

#include <cstddef>
#include <span>

namespace shine {

// Abstract bidirectional byte stream. Implemented by:
//   - TcpSocketStream : wraps a tcp::socket (inbound/outbound raw)
//   - ShineSessionStream : wraps a logical session on a shine link
// Implementers must be safe for use from one coroutine per direction.
class ISessionStream {
public:
    virtual ~ISessionStream() = default;

    // Read up to `buf.size()` bytes. Returns bytes read (0 = EOF).
    virtual awaitable<StatusOr<std::size_t>> read(std::span<u8> buf) = 0;

    // Write all of `data`. Returns Ok on success.
    virtual awaitable<Status> write(std::span<const u8> data) = 0;

    // Close the write side (half-close). After this, subsequent writes error.
    virtual awaitable<void> shutdownWrite() = 0;

    // Forcefully close both sides.
    virtual void close() = 0;
};

} // namespace shine
