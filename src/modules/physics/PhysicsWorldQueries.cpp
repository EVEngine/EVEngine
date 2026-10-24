#include "physics/PhysicsWorldQueries.h"
#include "common/CameraObstruction.h"
#include "common/ProcgenWorldQuery.h"
#include "physics/PhysicsCapabilities.h"
#include "physics/TargetingLineOfSightAdapter.h"
#include "physics/World3D.h"

#include <algorithm>
#include <vector>

namespace eve::physics {
namespace {
class CameraObstructionQueryImpl final : public eve::ICameraObstructionQuery {
public:
    void add(World3D* world, std::weak_ptr<const void> lifetime) {
        std::erase_if(worlds_, [](const RegisteredWorld& entry) { return entry.lifetime.expired(); });
        const auto found = std::find_if(worlds_.begin(), worlds_.end(),
                                        [world](const RegisteredWorld& entry) { return entry.world == world; });
        if (world && found == worlds_.end()) worlds_.push_back({world, std::move(lifetime)});
    }

    bool sphereCast(float fromX, float fromY, float fromZ, float toX, float toY, float toZ, float radius,
                    uint64_t maskBits, int ignoredBodyId, eve::CameraObstructionHit* out) override {
        if (out) *out = eve::CameraObstructionHit{};
        if (!out) return false;
        for (const RegisteredWorld& entry : worlds_) {
            auto     lifetime = entry.lifetime.lock();
            World3D* world    = entry.world;
            if (!lifetime || !world || !world->isValid()) continue;
            CameraSphereHit3D hit;
            if (!world->sphereCast(fromX, fromY, fromZ, toX, toY, toZ, radius, maskBits, ignoredBodyId, &hit)) continue;
            if (!out->hit || hit.fraction < out->fraction) {
                out->hit      = true;
                out->bodyId   = hit.bodyId;
                out->fraction = hit.fraction;
                out->x        = hit.x;
                out->y        = hit.y;
                out->z        = hit.z;
                out->normalX  = hit.nx;
                out->normalY  = hit.ny;
                out->normalZ  = hit.nz;
            }
        }
        return out->hit;
    }

private:
    struct RegisteredWorld {
        World3D*                  world = nullptr;
        std::weak_ptr<const void> lifetime;
    };

    std::vector<RegisteredWorld> worlds_;
};

CameraObstructionQueryImpl& cameraObstructionQueryImpl();

class ProcgenWorldQueryImpl final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY,
                                                    std::uint64_t maskBits) override {
        eve::ProcgenSurfaceHit    output;
        eve::CameraObstructionHit hit;
        const bool found = cameraObstructionQueryImpl().sphereCast(x, maxY, z, x, minY, z, 0.f, maskBits, -1, &hit);
        if (!found) return eve::Result<eve::ProcgenSurfaceHit>::success(output);
        output.hit      = true;
        output.objectId = hit.bodyId;
        output.x        = hit.x;
        output.y        = hit.y;
        output.z        = hit.z;
        output.normalX  = hit.normalX;
        output.normalY  = hit.normalY;
        output.normalZ  = hit.normalZ;
        return eve::Result<eve::ProcgenSurfaceHit>::success(output);
    }
};

CameraObstructionQueryImpl& cameraObstructionQueryImpl() {
    static CameraObstructionQueryImpl impl;
    return impl;
}

TargetingLineOfSightAdapter& targetingLineOfSightAdapterImpl() {
    static TargetingLineOfSightAdapter impl;
    return impl;
}

}  // namespace

eve::ICameraObstructionQuery& cameraObstructionQuery() { return cameraObstructionQueryImpl(); }
TargetingLineOfSightAdapter&  targetingLineOfSightAdapter() { return targetingLineOfSightAdapterImpl(); }
eve::IProcgenWorldQuery&      procgenWorldQuery() {
    static ProcgenWorldQueryImpl query;
    return query;
}

void registerCameraObstructionWorld(World3D* world) {
    if (world) cameraObstructionQueryImpl().add(world, world->queryLifetime_);
}
eve::Result<void> registerTargetingLineOfSightWorld(World3D* world) {
    return targetingLineOfSightAdapterImpl().addWorld(world);
}

}  // namespace eve::physics
