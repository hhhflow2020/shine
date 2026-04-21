#pragma once

#include "core/common.hpp"

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <memory>

namespace shine::metrics {

class Registry {
public:
    Registry();

    prometheus::Registry& raw() { return *registry_; }
    std::shared_ptr<prometheus::Registry> shared() { return registry_; }

private:
    std::shared_ptr<prometheus::Registry> registry_;

public:
    prometheus::Family<prometheus::Counter>&   inbound_connections;
    prometheus::Family<prometheus::Gauge>&     sessions_active;
    prometheus::Family<prometheus::Counter>&   sessions_opened;
    prometheus::Family<prometheus::Counter>&   sessions_closed;
    prometheus::Family<prometheus::Counter>&   bytes_total;
    prometheus::Family<prometheus::Histogram>& connect_latency;
    prometheus::Family<prometheus::Counter>&   errors_total;
    prometheus::Family<prometheus::Counter>&   resp2_decode_errors;
};

// Global singleton accessor.
Registry& instance();

} // namespace shine::metrics
