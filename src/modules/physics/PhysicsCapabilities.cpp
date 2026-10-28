#include "common/CameraObstruction.h"
#include "common/Capability.h"
#include "common/PhysicsQuery.h"
#include "common/ProcgenWorldQuery.h"
#include "common/WorldSphereSweep.h"
#include "physics/ArtifactProvider.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorldQueries.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/WorldSphereSweep.h"
#include "sensing/LineOfSightRouter.h"

#include <algorithm>
#include <string>
#include <vector>

namespace eve::physics {
namespace {

class PhysicsQueryImpl final : public eve::IPhysicsQuery {
public:
    int newWorld(float gravityX, float gravityY) override {
        auto *ph = eve::ModuleManager::getInstance<Physics>("Physics");
        if (!ph) return -1;
        auto *w = ph->newWorld(gravityX, gravityY);
        if (!w) return -1;
        worlds_.push_back(w);
        return static_cast<int>(worlds_.size()) - 1;
    }

    int worldCount() override {
        int n = 0;
        for (auto *w : worlds_)
            if (w) ++n;
        return n;
    }

    bool worldGravity(int id, float *gx, float *gy) override {
        auto *w = world(id);
        if (!w) return false;
        if (gx) *gx = w->getGravityX();
        if (gy) *gy = w->getGravityY();
        return true;
    }

    bool rayCast(int id, float x1, float y1, float x2, float y2, RayHitInfo *out) override {
        auto *w = world(id);
        if (!w || !out) return false;
        w->rayCast(x1, y1, x2, y2);
        out->hit = w->hasRayHit();
        if (out->hit) {
            out->bodyId = w->getRayHitBodyId();
            out->x = w->getRayHitX();
            out->y = w->getRayHitY();
            out->normalX = w->getRayHitNormalX();
            out->normalY = w->getRayHitNormalY();
            out->fraction = w->getRayHitFraction();
        }
        return true;
    }

    bool removeWorld(int id) override {
        if (id < 0 || static_cast<size_t>(id) >= worlds_.size()) return false;
        delete worlds_[static_cast<size_t>(id)];
        worlds_[static_cast<size_t>(id)] = nullptr;
        return true;
    }

private:
    World *world(int id) const {
        if (id < 0 || static_cast<size_t>(id) >= worlds_.size()) return nullptr;
        return worlds_[static_cast<size_t>(id)];
    }

    std::vector<World *> worlds_;
};

}  // namespace

void registerPhysicsCapabilities() {
    static PhysicsQueryImpl impl;
    eve::cap::provide<eve::IWorldSphereSweepFactory>(&worldSphereSweepFactory());
    eve::cap::provide<eve::IPhysicsQuery>(&impl);
    eve::cap::provide<eve::IProcgenWorldQuery>(&procgenWorldQuery());
    eve::cap::provide<eve::ICameraObstructionQuery>(&cameraObstructionQuery());
    // Claim World3D through the sensing router rather than owning the single-slot
    // `ILineOfSightQuery` capability: a grid board can then claim its own space without the two
    // providers replacing each other by load order. `ensureLineOfSightRouter` reports Conflict
    // when some other provider already holds the capability; that registration is left alone on
    // purpose, so this adapter simply does not take part and the project keeps its own provider.
    auto router = eve::sensing::ensureLineOfSightRouter();
    if (router)
        router.value()
            ->addProvider(eve::sensing::CoordinateSpace::World3D, &targetingLineOfSightAdapter())
            .ignore("physics line-of-sight space claim was refused");
    registerPhysicsArtifactProvider();
}

}  // namespace eve::physics
