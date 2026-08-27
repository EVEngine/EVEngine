#include "common/Capability.h"
#include "common/CameraObstruction.h"
#include "common/PhysicsQuery.h"
#include "physics/Physics.h"
#include "physics/ArtifactProvider.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World.h"
#include "physics/World3D.h"

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

class CameraObstructionQueryImpl final : public eve::ICameraObstructionQuery {
public:
    void add(World3D* world) {
        if (world && std::find(worlds_.begin(), worlds_.end(), world) == worlds_.end())
            worlds_.push_back(world);
    }
    void remove(World3D* world) {
        worlds_.erase(std::remove(worlds_.begin(), worlds_.end(), world), worlds_.end());
    }

    bool sphereCast(float fromX, float fromY, float fromZ, float toX, float toY, float toZ,
                    float radius, uint64_t maskBits, int ignoredBodyId,
                    eve::CameraObstructionHit* out) override {
        if (out) *out = eve::CameraObstructionHit{};
        if (!out) return false;
        for (World3D* world : worlds_) {
            if (!world || !world->isValid()) continue;
            CameraSphereHit3D hit;
            if (!world->sphereCast(fromX, fromY, fromZ, toX, toY, toZ, radius, maskBits,
                                   ignoredBodyId, &hit))
                continue;
            if (!out->hit || hit.fraction < out->fraction) {
                out->hit = true;
                out->bodyId = hit.bodyId;
                out->fraction = hit.fraction;
                out->x = hit.x;
                out->y = hit.y;
                out->z = hit.z;
                out->normalX = hit.nx;
                out->normalY = hit.ny;
                out->normalZ = hit.nz;
            }
        }
        return out->hit;
    }

private:
    std::vector<World3D*> worlds_;
};

CameraObstructionQueryImpl& cameraObstructionQuery() {
    static CameraObstructionQueryImpl impl;
    return impl;
}

TargetingLineOfSightAdapter& targetingLineOfSightAdapter() {
    static TargetingLineOfSightAdapter impl;
    return impl;
}

}  // namespace

void registerPhysicsCapabilities() {
    static PhysicsQueryImpl impl;
    eve::cap::provide<eve::IPhysicsQuery>(&impl);
    eve::cap::provide<eve::ICameraObstructionQuery>(&cameraObstructionQuery());
    eve::cap::provide<eve::sensing::ILineOfSightQuery>(&targetingLineOfSightAdapter());
    registerPhysicsArtifactProvider();
}

void registerCameraObstructionWorld(World3D* world) { cameraObstructionQuery().add(world); }
void unregisterCameraObstructionWorld(World3D* world) { cameraObstructionQuery().remove(world); }
eve::Result<void> registerTargetingLineOfSightWorld(World3D* world) {
    return targetingLineOfSightAdapter().addWorld(world);
}
eve::Result<void> unregisterTargetingLineOfSightWorld(World3D* world) {
    return targetingLineOfSightAdapter().removeWorld(world);
}

}  // namespace eve::physics
