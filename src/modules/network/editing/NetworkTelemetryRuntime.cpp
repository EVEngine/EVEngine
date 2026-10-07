#include "network/editing/NetworkTelemetry.h"
#include "network/Network.h"
#include <utility>
namespace eve::network_editing {

EditorResult<void> NetworkTelemetryCollector::collect(network::Network* source, double time,
                                                      NetworkTelemetryModel& model) const {
    if (!source)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.network.source"),
                                          "Network telemetry source is required");
    const auto s = source->telemetrySnapshot();
    NetworkTelemetrySample value;
    value.revision         = s.revision;
    value.sentBytes        = s.sentBytes;
    value.receivedBytes    = s.receivedBytes;
    value.completions      = s.completions;
    value.errors           = s.errors;
    value.connections      = s.connections;
    value.watchedTcp       = s.watchedTcp;
    value.watchedUdp       = s.watchedUdp;
    value.channels         = s.channels;
    value.queuedTcpBytes   = s.queuedTcpBytes;
    value.timeSeconds      = time;
    return model.ingest(value);
}

}  // namespace eve::network_editing
