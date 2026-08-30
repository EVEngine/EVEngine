#pragma once

/** @file CanonicalPcgCook.h @brief Canonical PCG graph compilation to runtime execution plans. */

#include "common/Result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::asset {

/** @brief Runtime metadata and deterministic PointGraph definition for one canonical PCG graph. */
struct CookedCanonicalPcgGraph {
    std::vector<std::uint8_t> definition;
    std::vector<std::uint8_t> executionPlan;
};

/**
 * @brief Compile `eve.pcg-graph/1` terrain scatter rules to a bounded PointGraph plan.
 * @param definition Canonical JSON definition owned by the caller for this call.
 * @param maximumRules Upper bound on accepted scatter rules and generated branches.
 * @param maximumPlanBytes Upper bound on the owning execution-plan result.
 * @return Transactional owning Cook result; malformed or unsupported graphs are rejected.
 * @thread Worker-safe; uses no shared mutable state.
 */
[[nodiscard]] Result<CookedCanonicalPcgGraph> cookCanonicalPcgGraph(
    std::span<const std::uint8_t> definition, std::uint32_t maximumRules,
    std::uint64_t maximumPlanBytes);

}  // namespace eve::asset
