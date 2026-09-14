#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace eve::stylize {

/** @brief Runtime quality tier selected for one mesh VFX instance. */
enum class MeshVfxLodTier : std::uint8_t {
    Full,
    Reduced,
    Minimal,
    Culled,
};

/** @brief Global deterministic quality and work budget for mesh VFX. */
struct MeshVfxScalabilityPolicy {
    float fullQualityMinPixels = 96.0f;
    float reducedQualityMinPixels = 32.0f;
    float maximumDistance = 80.0f;
    std::uint32_t workUnitBudget = 128;
    std::uint32_t fullWorkUnits = 4;
    std::uint32_t reducedWorkUnits = 2;
    std::uint32_t minimalWorkUnits = 1;
    std::uint32_t reducedTrailSampleStride = 2;
    std::uint32_t minimalTrailSampleStride = 4;
    std::uint32_t reducedMeshUpdateInterval = 2;
    std::uint32_t minimalMeshUpdateInterval = 4;
};

/** @brief View-dependent input used to prioritize one mesh VFX instance. */
struct MeshVfxLodCandidate {
    std::uint64_t stableInstanceId = 0;
    float distance = 0.0f;
    float projectedRadiusPixels = 0.0f;
    std::int32_t priority = 0;
    bool visible = true;
};

/** @brief Deterministic rendering decision for one input candidate. */
struct MeshVfxLodDecision {
    std::uint64_t stableInstanceId = 0;
    MeshVfxLodTier tier = MeshVfxLodTier::Culled;
    std::uint32_t trailSampleStride = 1;
    std::uint32_t meshUpdateInterval = 1;
    std::uint32_t workUnits = 0;

};

/**
 * @brief Selects mesh VFX quality under a deterministic per-view work budget.
 *
 * The planner owns no instance state and is safe to call from any thread. Equal
 * priority candidates are ordered by distance and then stableInstanceId, making
 * replay and backend decisions independent of input iteration order.
 */
class MeshVfxScalabilityPlanner {
public:
    /**
     * @brief Creates a planner with the supplied immutable policy.
     * @param policy Quality thresholds and work costs.
     */
    explicit MeshVfxScalabilityPlanner(MeshVfxScalabilityPolicy policy = {});

    /**
     * @brief Plans one frame while preserving the input order in the result.
     * @param candidates Per-instance view measurements.
     * @return One decision for every candidate.
     */
    [[nodiscard]] std::vector<MeshVfxLodDecision> plan(std::span<const MeshVfxLodCandidate> candidates) const;

    /** @brief Returns the policy used by this planner. */
    [[nodiscard]] const MeshVfxScalabilityPolicy& policy() const noexcept { return policy_; }

private:
    MeshVfxScalabilityPolicy policy_;
};

} // namespace eve::stylize
