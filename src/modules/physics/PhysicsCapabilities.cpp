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
    void add(World3D* world, std::weak_ptr<const void> lifetime) {
        std::erase_if(worlds_,
                      [](const RegisteredWorld& entry) { return entry.lifetime.expired(); });
        const auto found = std::find_if(
            worlds_.begin(), worlds_.end(),
            [world](const RegisteredWorld& entry) { return entry.world == world; });
        if (world && found == worlds_.end()) worlds_.push_back({world, std::move(lifetime)});
    }

    bool sphereCast(float fromX, float fromY, float fromZ, float toX, float toY, float toZ,
                    float radius, uint64_t maskBits, int ignoredBodyId,
                    eve::CameraObstructionHit* out) override {
        if (out) *out = eve::CameraObstructionHit{};
        if (!out) return false;
        for (const RegisteredWorld& entry : worlds_) {
            auto lifetime = entry.lifetime.lock();
            World3D* world = entry.world;
            if (!lifetime || !world || !world->isValid()) continue;
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
    struct RegisteredWorld {
        World3D* world = nullptr;
        std::weak_ptr<const void> lifetime;
    };

    std::vector<RegisteredWorld> worlds_;
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

void registerCameraObstructionWorld(World3D* world) {
    if (world) cameraObstructionQuery().add(world, world->queryLifetime_);
}
eve::Result<void> registerTargetingLineOfSightWorld(World3D* world) {
    return targetingLineOfSightAdapter().addWorld(world);
}

}  // namespace eve::physics
