#include "outbound/block_outbound.hpp"

#include "core/logging.hpp"

namespace shine {

awaitable<Status> BlockOutbound::handle(SessionRequest req) {
    SHINE_DEBUG("blocked target={} from inbound={}",
                req.target.toString(), req.inbound_tag);
    if (req.client_stream) {
        co_await req.client_stream->shutdownWrite();
        req.client_stream->close();
    }
    co_return absl::OkStatus();
}

} // namespace shine
