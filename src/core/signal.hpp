#pragma once

#include "core/common.hpp"

#include <functional>

namespace shine {

// Wait for SIGINT/SIGTERM and invoke handler. Runs inside io_context.
awaitable<void> waitForTermination(std::function<awaitable<void>()> onTerminate);

} // namespace shine
