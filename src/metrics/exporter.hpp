#pragma once

#include "config/schema.hpp"
#include "core/common.hpp"

#include <memory>

namespace prometheus { class Exposer; }

namespace shine::metrics {

class Exporter {
public:
    Exporter();
    ~Exporter();
    Status start(const config::MetricsConfig& cfg);
    void   stop();

private:
    std::unique_ptr<prometheus::Exposer> exposer_;
};

} // namespace shine::metrics
