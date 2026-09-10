#pragma once

#include "agent/Agent.h"
#include "common/Value.h"

namespace eve::agent {
/** @brief Decode strict script/JSON configuration; unknown keys rejected, no mutations or callbacks. */
[[nodiscard]] Result<Config> decodeConfig(const Value& value);
/** @brief Decode owning observation data, rejecting malformed values; dimensions are checked by run/infer. */
[[nodiscard]] Result<Observation> decodeObservation(const Value& value);
/** @brief Decode and validate version-1 owning policy; unknown keys/versions rejected atomically. */
[[nodiscard]] Result<Policy> decodePolicy(const Value& value);
/** @brief Decode version-1 owning replay data; run-time consistency is checked before replay reset. */
[[nodiscard]] Result<Trace> decodeTrace(const Value& value);
/** @brief Encode a runner-produced policy into owning versioned data; no references retained. */
[[nodiscard]] Value encodePolicy(const Policy& policy);
/** @brief Encode a runner-produced trace into owning versioned data; seeds use lossless decimal strings. */
[[nodiscard]] Value encodeTrace(const Trace& trace);
/** @brief Encode runner-produced report, including policy and replayable evidence, as owning data. */
[[nodiscard]] Value encodeReport(const Report& report);
}  // namespace eve::agent
