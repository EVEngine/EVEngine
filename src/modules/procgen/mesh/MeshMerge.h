#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "procgen/GtsMeshSimplifier.h"
#include "procgen/MeshBuild.h"
#include "procgen/mesh/MeshContactBlend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen {

/**
 * @brief Authoring plan for commit-style static mesh merge.
 *
 * Copies sources on append. Contact-band fusion is opt-in and off by default
 * (`enableContactBlend == false`). Owner-thread mutation; no callbacks retained.
 */
class EVENGINE_API_DOMAINS MeshMergePlan {
public:
    /** @brief Pivot placement for the merged mesh. */
    enum class PivotMode : std::uint8_t { FirstSource = 0, WorldOrigin = 1 };

    /**
     * @brief Copy one mesh after yaw/scale/translation into the plan.
     * @param defaultMaterialId Used when the source has no triangle groups.
     */
    [[nodiscard]] Result<void> appendSource(const MeshBuild& mesh, float tx, float ty, float tz, float yawDegrees,
                                            float sx, float sy, float sz, std::string defaultMaterialId = "default");
    void clear() noexcept;
    [[nodiscard]] int getSourceCount() const noexcept;

    [[nodiscard]] Result<void> setEnableContactBlend(bool enabled) noexcept;
    [[nodiscard]] bool         getEnableContactBlend() const noexcept { return enableContactBlend_; }
    [[nodiscard]] Result<void> setContactBlendParams(const MeshContactBlendParams& params);
    [[nodiscard]] const MeshContactBlendParams& contactBlendParams() const noexcept { return blendParams_; }
    /** @brief Positive weld tolerance enables hard weld; non-positive disables (default). */
    [[nodiscard]] Result<void> setWeldTolerance(float tolerance) noexcept;
    [[nodiscard]] float        getWeldTolerance() const noexcept { return weldTolerance_; }
    /**
     * @brief Optional GTS quality in (0,1); non-positive disables simplify (default).
     * When set, simplify runs after optional blend.
     */
    [[nodiscard]] Result<void> setSimplifyQuality(float quality) noexcept;
    [[nodiscard]] float        getSimplifyQuality() const noexcept { return simplifyQuality_; }
    [[nodiscard]] Result<void> setPivotMode(PivotMode mode) noexcept;
    [[nodiscard]] PivotMode    getPivotMode() const noexcept { return pivotMode_; }
    [[nodiscard]] Result<void> setMergeVertexColors(bool enabled) noexcept;
    [[nodiscard]] bool         getMergeVertexColors() const noexcept { return mergeVertexColors_; }

private:
    friend EVENGINE_API_DOMAINS Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan&);
    struct Source {
        MeshBuild   mesh;
        std::string defaultMaterialId;
    };
    std::vector<Source>    sources_;
    bool                   enableContactBlend_ = false;
    MeshContactBlendParams blendParams_{};
    float                  weldTolerance_     = 0.f;
    float                  simplifyQuality_   = 0.f;
    PivotMode              pivotMode_         = PivotMode::FirstSource;
    bool                   mergeVertexColors_ = true;
    std::string            falloffStorage_    = "smooth";
};

/**
 * @brief Merge planned static meshes into one owning MeshBuild.
 * @return Owning mesh or `procgen.mesh.merge.*` diagnostic; never partially publishes.
 * @thread Synchronous CPU; inputs borrowed only for the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan& plan);

}  // namespace eve::procgen
