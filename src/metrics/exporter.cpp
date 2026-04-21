#include "metrics/exporter.hpp"

#include "core/logging.hpp"
#include "metrics/registry.hpp"

#include <prometheus/exposer.h>

namespace shine::metrics {

Exporter::Exporter() = default;
Exporter::~Exporter() = default;

Status Exporter::start(const config::MetricsConfig& cfg) {
    try {
        exposer_ = std::make_unique<prometheus::Exposer>(cfg.listen);
        exposer_->RegisterCollectable(instance().shared());
        SHINE_INFO("metrics exporter on http://{}{}", cfg.listen, cfg.path);
        return absl::OkStatus();
    } catch (const std::exception& e) {
        return absl::UnavailableError(std::string("metrics exposer: ") + e.what());
    }
}

void Exporter::stop() {
    exposer_.reset();
}

} // namespace shine::metrics
