#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "physics/PhysicsHandles.h"

#include <cstdint>
#include <memory>

namespace eve::physics { class Body3D; class World3D; }
namespace eve::procgen { class GtsTerrainLodSet; }
namespace eve::procgen_physics {
/** @brief Transactional owner of optional static triangle-mesh colliders for a GTS tile batch. */
class EVENGINE_API_ORCHESTRATION GtsTerrainColliderRuntime {
public:
    GtsTerrainColliderRuntime();
    ~GtsTerrainColliderRuntime();
    GtsTerrainColliderRuntime(const GtsTerrainColliderRuntime&)=delete;
    GtsTerrainColliderRuntime& operator=(const GtsTerrainColliderRuntime&)=delete;
    /**
     * @brief Replace all colliders from one selected LOD after every candidate has succeeded.
     * @param lods Borrowed immutable LOD set consumed synchronously.
     * @param world Borrowed physics owner; world teardown safely makes stored handles stale.
     * @param collisionLevel Level used as collision geometry, normally zero.
     * @return New revision, or a structured failure preserving the previous live batch.
     * @thread Owning physics thread only; no callbacks or scripts are invoked.
     */
    [[nodiscard]] Result<std::uint64_t> replace(const procgen::GtsTerrainLodSet& lods,
        physics::World3D& world,int collisionLevel,float originX=0.f,float originY=0.f,float originZ=0.f);
    /** @brief Destroy every still-live collider body and advance the revision. */
    [[nodiscard]] Result<int> clear();
    /** @brief Resolve a borrowed collider body, or null for empty, stale or invalid tile slots. */
    [[nodiscard]] physics::Body3D* getBody(int tileIndex)const;
    /** @brief Return row-major tile slot count including empty cells. */
    [[nodiscard]] int getTileCount()const;
    /** @brief Return the latest committed revision. */
    [[nodiscard]] std::uint64_t getRevision()const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen_physics
