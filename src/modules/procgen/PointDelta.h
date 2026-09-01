#pragma once

#include "procgen/PointSet.h"

#include <cstdint>
#include <vector>

namespace eve::procgen {

/**
 * @brief Transactional identity-based change set between two point snapshots.
 *
 * Every participating point must have a unique non-zero id. The target order is
 * explicit so reordering is reproducible and does not degrade into remove/add churn.
 */
struct PointDelta {
    /** @brief Target-only points, including their complete attribute rows. */
    PointSet                   added;
    /** @brief Base identities whose point or attribute content changed. */
    PointSet                   updated;
    /** @brief Base identities absent from the target snapshot. */
    std::vector<std::uint64_t> removed;
    /** @brief Exact identity order of the target snapshot. */
    std::vector<std::uint64_t> targetOrder;
    /** @brief Fingerprint that the input snapshot must match before application. */
    std::uint64_t              baseFingerprint   = 0;
    /** @brief Fingerprint that the fully applied target snapshot must produce. */
    std::uint64_t              targetFingerprint = 0;
};

/** @brief Compute an exact deterministic fingerprint, rejecting missing or duplicate ids. */
[[nodiscard]] Result<std::uint64_t> fingerprintPointSet(const PointSet& points);
/** @brief Compute an identity-based delta without mutating either snapshot. */
[[nodiscard]] Result<PointDelta> diffPointSets(const PointSet& before, const PointSet& after);
/** @brief Apply a delta atomically, rejecting stale or internally inconsistent input. */
[[nodiscard]] Result<PointSet> applyPointDelta(const PointSet& base, const PointDelta& delta);

}  // namespace eve::procgen
