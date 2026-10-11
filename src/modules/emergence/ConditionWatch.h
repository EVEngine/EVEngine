#pragma once
#include "common/Export.h"

/**
 * @file ConditionWatch.h
 * @brief Derive inverted-index watch keys from a decision::Condition AST.
 */

#include "common/Result.h"
#include "decision/Condition.h"

#include <string>
#include <vector>

namespace eve::emergence {

/**
 * @brief Collect the canonical fact keys a condition depends on.
 *
 * Leaf nodes contribute their domain key. PolicyCall nodes contribute
 * `policy:<name>` plus every declared `scriptDeclaration.dependencies` entry
 * (already expected to be canonical fact keys). All/Any/Not recurse into children.
 *
 * @param condition Side-effect-free condition tree.
 * @return Sorted unique keys, or a structured failure when a PolicyCall lacks
 *         both a name and a dependency declaration that would make indexing safe.
 * @thread Pure; reentrant; no callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION eve::Result<std::vector<std::string>> collectWatchKeys(
    const decision::Condition& condition);

}  // namespace eve::emergence
