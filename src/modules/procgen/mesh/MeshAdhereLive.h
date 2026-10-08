#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include "procgen/mesh/MeshContactBlend.h"

#include <cstdint>
#include <string>

namespace eve::procgen {

/**
 * @brief Real-time dynamic fusion session: source A sticks to surface B with adjustable params.
 *
 * Owns copies of A/B baselines. Derived display mesh is recomputed on dirty evaluate.
 * Bake is optional freeze; RemoveSetup restores the A baseline. Owner-thread only; no callbacks.
 */
class EVENGINE_API_DOMAINS MeshAdhereLive {
public:
    /** @brief Activate with owning copies of A (movable) and B (surface). */
    [[nodiscard]] Result<void> activateResult(const MeshBuild& sourceA, const MeshBuild& surfaceB);
    [[nodiscard]] bool         isActive() const noexcept { return active_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] Result<void> setParamsResult(const MeshContactBlendParams& params);
    [[nodiscard]] const MeshContactBlendParams& params() const noexcept { return params_; }
    /** @brief Soft position snap defaults true for dynamic fusion. */
    [[nodiscard]] Result<void> setSoftSnapPositionsResult(bool enabled) noexcept;

    /** @brief Replace surface B (e.g. after pose change baked into mesh space). Marks dirty. */
    [[nodiscard]] Result<void> setSurfaceResult(const MeshBuild& surfaceB);
    /** @brief Replace source A baseline. Marks dirty. */
    [[nodiscard]] Result<void> setSourceResult(const MeshBuild& sourceA);

    /**
     * @brief Recompute derived mesh when dirty (or force).
     * @return Applied revision, or diagnostic without publishing a partial derived mesh.
     */
    [[nodiscard]] Result<std::uint64_t> evaluateResult(bool force = false);
    [[nodiscard]] bool                  isDirty() const noexcept { return dirty_; }

    /**
     * @brief Borrow derived mesh; null until first successful evaluate.
     * @ownership Borrowed from this session; callers must not delete the pointer.
     * @lifetime Valid until the next successful `evaluateResult`, `bakeToMeshResult`,
     *           `removeSetup`, or destruction of this session.
     */
    [[nodiscard]] const MeshBuild* derivedMesh() const noexcept;
    /**
     * @brief Borrow immutable source A baseline; null when inactive or empty.
     * @ownership Borrowed from this session; callers must not delete the pointer.
     * @lifetime Valid until `setSourceResult`, `removeSetup`, `bakeToMeshResult`,
     *           or destruction of this session.
     */
    [[nodiscard]] const MeshBuild* sourceMesh() const noexcept;

    /** @brief Drop derived state and restore inactive; keeps no derived publication. */
    void removeSetup() noexcept;
    /**
     * @brief Freeze current derived (evaluating if dirty) into an owning MeshBuild and deactivate.
     */
    [[nodiscard]] Result<MeshBuild> bakeToMeshResult();

private:
    bool                   active_ = false;
    bool                   dirty_  = false;
    std::uint64_t          revision_ = 0;
    MeshBuild              source_{};
    MeshBuild              surface_{};
    MeshBuild              derived_{};
    MeshContactBlendParams params_{};
    std::string            falloffStorage_ = "smooth";
};

/**
 * @brief One-shot adhere of A onto B (soft snap on) using contact blend.
 * @return Owning deformed A, or diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshBuild> meshAdhereResult(const MeshBuild& sourceA,
                                                                      const MeshBuild& surfaceB,
                                                                      const MeshContactBlendParams& params);

}  // namespace eve::procgen
