#include "inbound/factory.hpp"

#include "inbound/http_connect_inbound.hpp"
#include "inbound/shine_inbound.hpp"
#include "inbound/socks5_inbound.hpp"

#include <absl/strings/str_cat.h>

namespace shine {

StatusOr<InboundPtr> makeInbound(asio::any_io_executor ex,
                                 const config::InboundConfig& cfg) {
    if (cfg.protocol == "socks5") {
        return std::static_pointer_cast<IInbound>(
            std::make_shared<Socks5Inbound>(std::move(ex), cfg));
    }
    if (cfg.protocol == "http-connect") {
        return std::static_pointer_cast<IInbound>(
            std::make_shared<HttpConnectInbound>(std::move(ex), cfg));
    }
    if (cfg.protocol == "shine") {
        return std::static_pointer_cast<IInbound>(
            std::make_shared<ShineInbound>(std::move(ex), cfg));
    }
    return absl::InvalidArgumentError(
        absl::StrCat("unknown inbound protocol: ", cfg.protocol));
}

} // namespace shine
