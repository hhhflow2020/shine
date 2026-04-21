#pragma once

#include "config/schema.hpp"
#include "core/common.hpp"
#include "inbound/inbound.hpp"

namespace shine {

StatusOr<InboundPtr> makeInbound(asio::any_io_executor ex,
                                 const config::InboundConfig& cfg);

} // namespace shine
