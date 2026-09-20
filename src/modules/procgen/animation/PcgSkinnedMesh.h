#pragma once

#include "animation/AnimSkin.h"
#include "common/Result.h"
#include "procgen/PcgMeshLod.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::procgen_animation {

/** @brief Owned ordered inputs for Pcg's skinned MeshCombiner path. */
class EVENGINE_API_ORCHESTRATION PcgSkinnedMeshPlan {
public:
    /** @brief Copy one mesh, transform and complete skin streams into the plan. */
    [[nodiscard]] Result<void> appendSource(const procgen::MeshBuild& mesh,
                                            const animation::AnimSkin& skin,
                                            const procgen::PcgMeshTransform& transform,
                                            const std::string& defaultMaterialId);
    /** @brief Remove all sources. */
    void clear() noexcept { sources_.clear(); }
    /** @brief Return the ordered source count. */
    [[nodiscard]] int getSourceCount() const noexcept { return static_cast<int>(sources_.size()); }

private:
    struct Source {
        procgen::MeshBuild mesh;
        procgen::PcgMeshTransform transform;
        std::string defaultMaterialId;
        animation::AnimSkinStreamData skin;
    };
    friend EVENGINE_API_ORCHESTRATION Result<void> combinePcgSkinnedMeshesInto(class PcgSkinnedMeshResult&,
                                                      const PcgSkinnedMeshPlan&);
    std::vector<Source> sources_;
};

/** @brief Atomically published combined mesh and matching owned AnimSkin. */
class PcgSkinnedMeshResult {
public:
    /** @brief Borrow the combined CPU mesh.
     * @ownership This result retains ownership.
     * @lifetime Valid until the next successful combine, move or destruction. */
    [[nodiscard]] const procgen::MeshBuild* mesh() const noexcept { return skin_ ? &mesh_ : nullptr; }
    /** @brief Borrow the skin whose vertex order exactly matches mesh().
     * @ownership This result retains ownership.
     * @lifetime Valid until the next successful combine, move or destruction. */
    [[nodiscard]] animation::AnimSkin* skin() const noexcept { return skin_.get(); }
    /** @brief Return whether a complete mesh/skin pair is present. */
    [[nodiscard]] bool valid() const noexcept { return skin_ != nullptr; }

private:
    friend EVENGINE_API_ORCHESTRATION Result<void> combinePcgSkinnedMeshesInto(PcgSkinnedMeshResult&, const PcgSkinnedMeshPlan&);
    procgen::MeshBuild mesh_;
    std::unique_ptr<animation::AnimSkin> skin_;
};

/**
 * @brief Combine Pcg skinned meshes, preserving its bone and bindpose remap semantics.
 * @param output Replaced only after both mesh and skin validate completely.
 * @param plan Immutable owning sources.
 * @return Success or a structured diagnostic without changing output.
 * @thread Synchronous CPU operation; callers serialize output access.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<void> combinePcgSkinnedMeshesInto(PcgSkinnedMeshResult& output,
                                                        const PcgSkinnedMeshPlan& plan);

}  // namespace eve::procgen_animation
