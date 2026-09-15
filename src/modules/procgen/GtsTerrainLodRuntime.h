#pragma once

#include "common/Result.h"

#include <cstdint>
#include <memory>

namespace eve::graphics { class Graphics; class Material; class Renderable3D; }
namespace eve::procgen {
class GtsTerrainLodSet;
class Procgen;

/** @brief Transactional runtime owner for one row-major batch of GTS terrain tile renderables. */
class GtsTerrainLodRuntime {
public:
    GtsTerrainLodRuntime();
    ~GtsTerrainLodRuntime();
    GtsTerrainLodRuntime(const GtsTerrainLodRuntime&)=delete;
    GtsTerrainLodRuntime& operator=(const GtsTerrainLodRuntime&)=delete;
    /**
     * @brief Replace the complete tile batch after configuring every non-empty candidate.
     * @return New non-zero revision, or a failure that leaves the old batch and revision unchanged.
     * @thread Main/render thread only.
     * @reentrant Not reentrant; no script callbacks are invoked.
     */
    [[nodiscard]] Result<std::uint64_t> replace(const GtsTerrainLodSet& lods,Procgen& procgen,
        graphics::Graphics& gfx,float worldDiameter,float verticalFovDegrees,
        float originX=0.f,float originY=0.f,float originZ=0.f);
    /**
     * @brief Replace the mesh batch and hide one source terrain only after successful publication.
     * @param sourceTerrain Borrowed ECS terrain renderable; its entering visibility is restored by clear or teardown.
     * @return New revision, or a failure leaving both the old batch and source visibility unchanged.
     * @thread Main/render thread only; no callbacks or scripts are invoked.
     */
    [[nodiscard]] Result<std::uint64_t> replaceAndHideSource(const GtsTerrainLodSet& lods,Procgen& procgen,
        graphics::Graphics& gfx,graphics::Renderable3D& sourceTerrain,float worldDiameter,float verticalFovDegrees,
        float originX=0.f,float originY=0.f,float originZ=0.f);
    /** @brief Resolve the currently hidden source terrain, or null after source destruction. */
    [[nodiscard]] graphics::Renderable3D* getSourceTerrain() const;
    /** @brief Destroy every live tile entity and advance the revision. */
    [[nodiscard]] Result<int> clear();
    /**
     * @brief Assign one Graphics-owned material to every live tile renderable.
     * @param material Borrowed material that must outlive all configured renderables.
     * @return Number of live tile renderables updated.
     */
    [[nodiscard]] Result<int> applyMaterial(graphics::Material& material);
    /** @brief Return the number of row-major tile slots, including empty cells. */
    [[nodiscard]] int getTileCount()const;
    /** @brief Return the current committed revision. */
    [[nodiscard]] std::uint64_t getRevision()const;
    /**
     * @brief Borrow a live tile renderable or return nullptr for empty, stale or invalid slots.
     * @ownership The ECS table retains ownership; the caller must not delete the pointer.
     * @lifetime Valid until ECS mutation or the next replace/clear operation.
     */
    [[nodiscard]] graphics::Renderable3D* getRenderable(int tileIndex)const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
