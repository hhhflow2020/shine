#include "outbound/factory.hpp"

#include "outbound/block_outbound.hpp"
#include "outbound/direct_outbound.hpp"
#include "outbound/http_connect_outbound.hpp"
#include "outbound/shine_outbound.hpp"
#include "outbound/socks5_outbound.hpp"

#include <absl/strings/str_cat.h>

namespace shine {

StatusOr<OutboundPtr> makeOutbound(asio::any_io_executor ex,
                                   const config::OutboundConfig& cfg) {
    if (cfg.protocol == "direct") {
        return std::static_pointer_cast<IOutbound>(
            std::make_shared<DirectOutbound>(std::move(ex), cfg));
    }
    if (cfg.protocol == "block") {
        return std::static_pointer_cast<IOutbound>(
            std::make_shared<BlockOutbound>(cfg));
    }
    if (cfg.protocol == "shine-client") {
        return std::static_pointer_cast<IOutbound>(
            std::make_shared<ShineOutbound>(std::move(ex), cfg));
    }
    if (cfg.protocol == "socks5") {
        return std::static_pointer_cast<IOutbound>(
            std::make_shared<Socks5Outbound>(std::move(ex), cfg));
    }
    if (cfg.protocol == "http-connect") {
        return std::static_pointer_cast<IOutbound>(
            std::make_shared<HttpConnectOutbound>(std::move(ex), cfg));
    }
    return absl::InvalidArgumentError(
        absl::StrCat("unknown outbound protocol: ", cfg.protocol));
}

} // namespace shine
