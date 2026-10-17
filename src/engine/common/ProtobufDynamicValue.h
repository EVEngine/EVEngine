#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

namespace eve::persistence::proto {
class DynamicValue;
}

namespace eve::proto_support {

/**
 * @brief Encode open-ended JSON-compatible data without losing Int64 precision.
 * @param source Dynamic runtime value to encode.
 * @param target Owned Protobuf destination, replaced only on success.
 * @return Success, or a structured failure leaving target unchanged.
 * @cost Linear in the recursively owned value size per serialization operation.
 * @thread Caller owns both values; no shared state or callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> encodeDynamicValue(const Value&                      source,
                                                                      persistence::proto::DynamicValue& target);

/**
 * @brief Decode and validate open-ended Protobuf data as a dynamic runtime value.
 * @param source Borrowed Protobuf value.
 * @return Independent value, or a structured parse failure.
 * @cost Linear in the recursively encoded value size per restore operation.
 * @thread No shared state or callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> decodeDynamicValue(const persistence::proto::DynamicValue& source);

}  // namespace eve::proto_support
