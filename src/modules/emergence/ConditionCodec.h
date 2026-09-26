#pragma once
#include "common/Export.h"

/**
 * @file ConditionCodec.h
 * @brief Value/JSON codec for decision::Condition trees owned by emergence rules.
 */

#include "common/Result.h"
#include "common/Value.h"
#include "decision/Condition.h"

namespace eve::emergence {

/**
 * @brief Decode one validated condition tree from an owning Value.
 * @param value Node-shaped Value from an `eve.emergence.rules` document.
 * @return Validated condition tree, or a structured parse failure.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<decision::Condition> decodeCondition(const eve::Value& value);

}  // namespace eve::emergence
