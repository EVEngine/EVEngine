#pragma once

#include "common/Export.h"
#include "common/Snapshot.h"

#include <string>
#include <string_view>

namespace eve::proto_support {

/** @brief Verified stable header and owned typed payload from a binary snapshot. */
struct BinarySnapshotData {
    std::string    type;
    LogicalId      schema;
    SchemaVersion  schemaVersion;
    PersistentId   instanceId;
    Revision       revision;
    SimulationTick tick;
    std::string    payloadWire;
};

/**
 * @brief Seal typed Protobuf payload bytes in the shared binary snapshot envelope.
 * @param data Stable header and owned payload bytes; its schema identifies the payload message.
 * @param hashProvider Injected digest provider receiving deterministic Protobuf bytes without the digest field.
 * @return Complete binary envelope, or a structured validation or hash failure.
 * @cost Linear in payload bytes per snapshot, including one digest and two envelope serializations.
 * @thread No shared state or callbacks except the caller's hash provider.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> serializeBinarySnapshot(
    const BinarySnapshotData& data, const SnapshotHashProvider& hashProvider);

/**
 * @brief Parse and verify a shared binary snapshot before exposing its payload.
 * @param bytes Complete BinarySnapshotEnvelope wire message.
 * @param hashProvider Injected digest provider used when the envelope was sealed.
 * @return Verified stable header and owned payload bytes, or a structured failure.
 * @cost Linear in envelope and payload bytes per restore.
 * @thread No shared state or callbacks except the caller's hash provider.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<BinarySnapshotData> parseBinarySnapshot(
    std::string_view bytes, const SnapshotHashProvider& hashProvider);

}  // namespace eve::proto_support
