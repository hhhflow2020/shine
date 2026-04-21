#include "metrics/registry.hpp"

#include <prometheus/family.h>

namespace shine::metrics {

Registry::Registry()
    : registry_(std::make_shared<prometheus::Registry>()),
      inbound_connections(
          prometheus::BuildCounter()
              .Name("shine_inbound_connections_total")
              .Help("Total inbound connections accepted")
              .Register(*registry_)),
      sessions_active(
          prometheus::BuildGauge()
              .Name("shine_sessions_active")
              .Help("Currently active proxy sessions")
              .Register(*registry_)),
      sessions_opened(
          prometheus::BuildCounter()
              .Name("shine_sessions_opened_total")
              .Help("Proxy sessions opened")
              .Register(*registry_)),
      sessions_closed(
          prometheus::BuildCounter()
              .Name("shine_sessions_closed_total")
              .Help("Proxy sessions closed")
              .Register(*registry_)),
      bytes_total(
          prometheus::BuildCounter()
              .Name("shine_bytes_total")
              .Help("Bytes transferred")
              .Register(*registry_)),
      connect_latency(
          prometheus::BuildHistogram()
              .Name("shine_connect_latency_seconds")
              .Help("Outbound dial latency")
              .Register(*registry_)),
      errors_total(
          prometheus::BuildCounter()
              .Name("shine_errors_total")
              .Help("Errors by kind")
              .Register(*registry_)),
      resp2_decode_errors(
          prometheus::BuildCounter()
              .Name("shine_resp2_decode_errors_total")
              .Help("RESP2 decode errors")
              .Register(*registry_))
{}

Registry& instance() {
    static Registry r;
    return r;
}

} // namespace shine::metrics
