#pragma once

#include "core/common.hpp"
#include "transport/session_stream.hpp"

#include <memory>

namespace shine {

// Bidirectional copy between two ISessionStreams. Returns when either side
// signals EOF or errors. Both streams' closures are invoked on exit.
awaitable<void> bidiCopy(std::shared_ptr<ISessionStream> a,
                         std::shared_ptr<ISessionStream> b);

} // namespace shine
