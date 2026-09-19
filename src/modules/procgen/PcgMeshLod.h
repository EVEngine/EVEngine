#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "procgen/GtsMeshSimplifier.h"

#include <vector>
#include <array>
#include <string>

namespace eve::procgen {

class PcgMeshCombinePlan;

/**
 * @brief Combine static meshes with Pcg material deduplication and root-relative transforms.
 * @param output Replaced only after all streams, material groups and budgets validate.
 * @param plan Immutable owning source plan; output cannot alias its copied meshes.
 * @return Combined triangle count or a structured diagnostic without changing output.
 * @thread Synchronous CPU operation; no callbacks or references are retained.
 *
 * Declared ahead of the classes that friend it: the first declaration in a
 * translation unit must carry the link-group macro, otherwise the friend
 * declarations inside PcgMeshTransform/PcgMeshCombinePlan fix a different
 * linkage and this dllexport declaration would be C2375.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> combinePcgStaticMeshesInto(MeshBuild& output, const PcgMeshCombinePlan& plan);

/** @brief Pcg/Unity renderer policies retained per generated mesh LOD. */
struct PcgMeshLodRendererState {
    int skinQuality = 0;                 // Auto, Bone1, Bone2, Bone4
    int shadowCastingMode = 1;           // Off, On, TwoSided, ShadowsOnly
    bool receiveShadows = true;
    int motionVectorMode = 1;            // Camera, Object, ForceNoMotion
    bool skinnedMotionVectors = true;
    int lightProbeUsage = 1;             // Off, BlendProbes, UseProxyVolume, CustomProvided
    int reflectionProbeUsage = 1;        // Off, BlendProbes, Simple
};

/** @brief Row-major affine matrix used by Pcg's root-relative static mesh combination. */
class PcgMeshTransform {
public:
    /** @brief Construct an identity transform. */
    PcgMeshTransform() noexcept;
    /** @brief Set one finite row/column element in the 4x4 matrix. */
    [[nodiscard]] Result<void> setElement(int row, int column, float value);
    /** @brief Return one element, or zero for an invalid coordinate. */
    [[nodiscard]] float getElement(int row, int column) const noexcept;
private:
    friend class PcgMeshCombinePlan;
    friend EVENGINE_API_DOMAINS Result<int> combinePcgStaticMeshesInto(MeshBuild&, const PcgMeshCombinePlan&);
    std::array<float, 16> matrix_{};
};

/**
 * @brief Owned ordered input plan for Pcg static MeshCombiner.
 *
 * Meshes and matrices are copied on append. A source without triangle groups uses
 * `defaultMaterialId`; grouped meshes use group names as stable material identities.
 * No renderer, material object or callback is retained. Callers serialize mutation.
 */
class EVENGINE_API_DOMAINS PcgMeshCombinePlan {
public:
    /** @brief Copy one readable mesh and root-relative affine transform into the plan. */
    [[nodiscard]] Result<void> appendSource(const MeshBuild& mesh, const PcgMeshTransform& transform,
                                            const std::string& defaultMaterialId);
    /** @brief Remove every owned source. */
    void clear() noexcept;
    /** @brief Return the ordered source count. */
    [[nodiscard]] int getSourceCount() const noexcept;
private:
    struct Source { MeshBuild mesh; PcgMeshTransform transform; std::string defaultMaterialId; };
    friend EVENGINE_API_DOMAINS Result<int> combinePcgStaticMeshesInto(MeshBuild&, const PcgMeshCombinePlan&);
    std::vector<Source> sources_;
};

/** @brief One UnityMeshSimplifierPcg LOD level translated to native mesh settings. */
struct PcgMeshLodLevel {
    float screenRelativeTransitionHeight = 0.5F;
    float fadeTransitionWidth = 0.0F;
    float quality = 1.0F;
    bool combineMeshes = false;
    bool combineSubMeshes = false;
    GtsMeshSimplificationOptions simplification{};
    PcgMeshLodRendererState renderer{};
};

/**
 * @brief Mutable value profile for at most four native renderer LOD levels.
 *
 * The profile owns its level records. It has no renderer, mesh, callback or thread
 * affinity and callers must serialize mutation. Level transitions must be strictly
 * descending when compiled.
 */
class PcgMeshLodProfile {
public:
    /** @brief Append one level after validating its scalar ranges. */
    [[nodiscard]] Result<void> appendLevel(float transitionHeight, float fadeWidth, float quality,
                                           bool combineMeshes = false, bool combineSubMeshes = false);
    /** @brief Replace one level's renderer policy after validating every Pcg enum value. */
    [[nodiscard]] Result<void> setLevelRendererState(int index, int skinQuality, int shadowCastingMode,
                                                     bool receiveShadows, int motionVectorMode,
                                                     bool skinnedMotionVectors, int lightProbeUsage,
                                                     int reflectionProbeUsage);
    /** @brief Return one renderer state field (0..6), or -1 for invalid input. */
    [[nodiscard]] int getLevelRendererState(int index, int field) const noexcept;
    /** @brief Configure Unity LODFadeMode (0 None, 1 SpeedTree, 2 CrossFade) and optional time animation. */
    [[nodiscard]] Result<void> setFadePolicy(int fadeMode, bool animateCrossFading,
                                             float animationDuration = 0.5F);
    /** @brief Return configured Unity LODFadeMode. */
    [[nodiscard]] int getFadeMode() const noexcept { return fadeMode_; }
    /** @brief Return whether transitions advance from injected dt. */
    [[nodiscard]] bool getAnimateCrossFading() const noexcept { return animateCrossFading_; }
    /** @brief Return time-driven transition duration in seconds. */
    [[nodiscard]] float getCrossFadeAnimationDuration() const noexcept { return crossFadeAnimationDuration_; }
    /** @brief Remove all level records. */
    void clear() noexcept { levels_.clear(); }
    /** @brief Return the number of configured levels. */
    [[nodiscard]] int getLevelCount() const noexcept { return static_cast<int>(levels_.size()); }
    /** @brief Return an immutable level, or null for an invalid index.
     * @ownership The profile retains ownership.
     * @lifetime Valid until the next profile mutation or destruction. */
    [[nodiscard]] const PcgMeshLodLevel* levelAt(int index) const noexcept;
    /** @brief Borrow all levels for synchronous native compilation.
     * @ownership The profile retains ownership.
     * @lifetime The reference is invalidated by profile mutation or destruction. */
    [[nodiscard]] const std::vector<PcgMeshLodLevel>& levels() const noexcept { return levels_; }
private:
    std::vector<PcgMeshLodLevel> levels_;
    int fadeMode_ = 0;
    bool animateCrossFading_ = false;
    float crossFadeAnimationDuration_ = 0.5F;
};

/**
 * @brief Owned generic mesh LOD output generated with Pcg's independent-per-level policy.
 *
 * Every generated level is simplified from the same immutable source mesh, matching
 * `LODGenerator.CreateLevelRenderer`; levels are not simplified sequentially. The set
 * owns all CPU meshes and values and is safe to move. Callers serialize access.
 */
class PcgMeshLodSet {
public:
    /** @brief Return the number of complete generated levels. */
    [[nodiscard]] int getLevelCount() const noexcept { return static_cast<int>(meshes_.size()); }
    /** @brief Return one level's transition height, or -1 for an invalid index. */
    [[nodiscard]] float getTransitionHeight(int index) const noexcept;
    /** @brief Return one level's fade width, or -1 for an invalid index. */
    [[nodiscard]] float getFadeWidth(int index) const noexcept;
    /** @brief Return one level's quality, or -1 for an invalid index. */
    [[nodiscard]] float getQuality(int index) const noexcept;
    /** @brief Select a level from projected relative height; -1 means culled and -2 invalid input. */
    [[nodiscard]] int selectLevel(float relativeHeight) const noexcept;
    /** @brief Convert one level threshold to camera distance, or -1 for invalid input. */
    [[nodiscard]] float getSwitchDistance(int level, float worldDiameter, float verticalFovDegrees) const noexcept;
    /** @brief Borrow a generated mesh, or null for an invalid index.
     * @ownership The set retains ownership.
     * @lifetime Valid until replacement, move or destruction of this set. */
    [[nodiscard]] const MeshBuild* meshAt(int index) const noexcept;
    /** @brief Borrow one generated level's immutable renderer state, or null. */
    [[nodiscard]] const PcgMeshLodRendererState* rendererStateAt(int index) const noexcept;
    /** @brief Return Unity LODFadeMode copied from the build profile. */
    [[nodiscard]] int getFadeMode() const noexcept { return fadeMode_; }
    /** @brief Return whether transitions use injected time. */
    [[nodiscard]] bool getAnimateCrossFading() const noexcept { return animateCrossFading_; }
    /** @brief Return injected-time transition duration. */
    [[nodiscard]] float getCrossFadeAnimationDuration() const noexcept { return crossFadeAnimationDuration_; }
private:
    friend Result<void> buildPcgMeshLodsInto(PcgMeshLodSet&, const MeshBuild&, const PcgMeshLodProfile&);
    std::vector<PcgMeshLodLevel> levels_;
    std::vector<MeshBuild> meshes_;
    int fadeMode_ = 0;
    bool animateCrossFading_ = false;
    float crossFadeAnimationDuration_ = 0.5F;
};

/**
 * @brief Generate a complete generic Pcg LOD chain and publish it atomically.
 * @param output Replaced only after every level has simplified successfully.
 * @param source Immutable source mesh; it may be a mesh borrowed from another owner.
 * @param profile Immutable owned settings with one through four descending levels.
 * @return Success or a structured diagnostic without changing output.
 * @thread Synchronous CPU operation; no callbacks or borrowed references are retained.
 */
[[nodiscard]] Result<void> buildPcgMeshLodsInto(PcgMeshLodSet& output, const MeshBuild& source,
                                                 const PcgMeshLodProfile& profile);

/**
 * @brief Combine an ordered static-renderer plan and generate every combined LOD atomically.
 * @param output Replaced only after the combine and every simplification succeed.
 * @param plan Immutable owning source plan.
 * @param profile Immutable profile whose levels must all request mesh combination.
 * @return Success or a structured diagnostic without changing output.
 * @thread Synchronous CPU operation; no callbacks or borrowed references are retained.
 */
[[nodiscard]] Result<void> buildPcgCombinedMeshLodsInto(PcgMeshLodSet& output,
                                                         const PcgMeshCombinePlan& plan,
                                                         const PcgMeshLodProfile& profile);

}  // namespace eve::procgen
