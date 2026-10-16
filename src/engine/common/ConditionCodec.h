#pragma once
#include "common/Export.h"

/**
 * @file ConditionCodec.h
 * @brief Shared Value codec for thin condition::Condition trees.
 *
 * Encode emits only All/Any/Not/Compare/PolicyCall. Decode additionally accepts
 * legacy leaf kinds (`has_tag`, `has_attribute`, `has_resource`, `state_equals`,
 * `authority_check`) and expands them into Compare / PolicyCall conventions.
 */

#include "common/Condition.h"
#include "common/Result.h"
#include "common/Value.h"

namespace eve::condition {

/**
 * @brief Encode one validated condition tree as an owning Value.
 * @param condition Immutable condition tree to encode.
 * @return Node-shaped Value, or InvalidArgument when the tree is invalid.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<eve::Value> encodeCondition(const Condition& condition);

/**
 * @brief Decode one condition tree from an owning Value.
 * @param value Node-shaped Value; legacy leaf kinds are expanded.
 * @return Validated condition tree, or a structured parse failure.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<Condition> decodeCondition(const eve::Value& value);

}  // namespace eve::condition
