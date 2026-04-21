#pragma once

#include "config/schema.hpp"
#include "core/common.hpp"
#include "outbound/outbound.hpp"

namespace shine {

StatusOr<OutboundPtr> makeOutbound(asio::any_io_executor ex,
                                   const config::OutboundConfig& cfg);

} // namespace shine
