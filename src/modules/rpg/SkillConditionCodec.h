#pragma once

/**
 * @file SkillConditionCodec.h
 * @brief RPG-owned Value codec for the shared decision condition tree.
 */

#include "common/Result.h"
#include "common/Value.h"
#include "decision/Condition.h"

namespace eve::rpg {

/**
 * @brief Encode one validated Skill condition tree as an owning Value.
 * @param condition Immutable condition tree to encode.
 * @return Node-shaped Value, or InvalidArgument when the tree is invalid.
 */
[[nodiscard]] eve::Result<eve::Value> encodeSkillCondition(const eve::decision::Condition& condition);

/**
 * @brief Decode one Skill condition tree from an owning Value.
 * @param value Node-shaped Value from a canonical Skill definition.
 * @return Validated condition tree, or a structured parse failure.
 */
[[nodiscard]] eve::Result<eve::decision::Condition> decodeSkillCondition(const eve::Value& value);

}  // namespace eve::rpg
