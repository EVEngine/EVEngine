#pragma once

#include "agent/Agent.h"
#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::agent {

/**
 * @brief Serialize validated policy weights as versioned typed Protobuf bytes.
 * @return Owning wire bytes, or a structured validation/serialization failure.
 * @cost Linear in the full weight count per save; call at persistence boundaries.
 * @thread Caller owns the policy and prevents concurrent mutation.
 * @reentrancy No callbacks or shared mutable registry are used.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> policyToBinary(const Policy& policy);

/** @brief Parse and validate a versioned policy; unknown fields and versions are rejected atomically.
 * @cost Linear in the full wire size per import.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Policy> policyFromBinary(std::string_view bytes);

/** @brief Export the same typed policy as Protobuf JSON for inspection.
 * @cost Linear in the full weight count per export.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> policyToJson(const Policy& policy);

/** @brief Import strict Protobuf JSON policy data without changing live state.
 * @cost Linear in the full document size per import.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Policy> policyFromJson(std::string_view json);

/** @brief Serialize validated replay evidence as versioned typed Protobuf bytes.
 * @cost Linear in all observations and actions per save.
 * @thread Caller owns the trace and prevents concurrent mutation.
 * @reentrancy No callbacks or environment reset occur.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> traceToBinary(const Trace& trace);

/** @brief Parse and validate replay evidence before any environment is reset.
 * @cost Linear in the full trace wire size per import.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Trace> traceFromBinary(std::string_view bytes);

/** @brief Export validated replay evidence as Protobuf JSON for inspection.
 * @cost Linear in the full trace size per export.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::string> traceToJson(const Trace& trace);

/** @brief Import strict Protobuf JSON replay evidence without running it.
 * @cost Linear in the full document size per import.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Trace> traceFromJson(std::string_view json);

}  // namespace eve::agent
