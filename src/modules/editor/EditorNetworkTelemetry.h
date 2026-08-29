#pragma once
#include "editor/EditorProtocol.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
namespace eve::network { class Network; }
namespace eve::editor {
/** @brief One copied network telemetry sample; safe after the runtime module changes. */
struct NetworkTelemetrySample {
    std::uint64_t revision=0,sentBytes=0,receivedBytes=0,completions=0,errors=0,connections=0;
    std::size_t watchedTcp=0,watchedUdp=0,channels=0,queuedTcpBytes=0;
    double timeSeconds=0,sendBytesPerSecond=0,receiveBytesPerSecond=0,errorRate=0;
};
/** @brief UI-independent bounded history and network health diagnostics. */
class NetworkTelemetryModel {
public:
    explicit NetworkTelemetryModel(std::size_t capacity=300);
    EditorResult<void> ingest(NetworkTelemetrySample);
    const std::deque<NetworkTelemetrySample>& samples() const { return samples_; }
    std::vector<EditorDiagnostic> diagnostics() const;
    void clear();
private:
    std::size_t capacity_;
    std::deque<NetworkTelemetrySample> samples_;
};
/** @brief Optional bridge collecting a copied snapshot from the Network module. */
class NetworkTelemetryCollector {
public:
    EditorResult<void> collect(network::Network*,double,NetworkTelemetryModel&) const;
};
} // namespace eve::editor
