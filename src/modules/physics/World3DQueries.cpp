#include "physics/World3DInternal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace eve::physics {
using namespace world3d_detail;

bool World3D::sphereCast(float x1, float y1, float z1, float x2, float y2, float z2, float radius, uint64_t maskBits,
                         int ignoredBodyId, CameraSphereHit3D *out) const {
    if (!out) return false;
    *out = worldValue(rigidBackend_->sphereCast(x1, y1, z1, x2, y2, z2, radius, maskBits, ignoredBodyId));
    return out->hit;
}


int World3D::rayCast(float x1, float y1, float z1, float x2, float y2, float z2) {
    rayCastAll(x1, y1, z1, x2, y2, z2, 1);
    return rayHitBodyId_;
}

int World3D::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2, uint64_t maskBits) {
    return worldValue(rigidBackend_->rayCastFiltered(x1, y1, z1, x2, y2, z2, maskBits));
}


eve::Result<RayHit3D> World3D::rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                            QueryFilter3D filter) {
    return rigidBackend_->rayCastOwned(x1, y1, z1, x2, y2, z2, filter);
}


int World3D::rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) {
    return worldValue(rigidBackend_->rayCastAll(x1, y1, z1, x2, y2, z2, maxHits));
}


int World3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    return worldValue(rigidBackend_->queryAABB(minX, minY, minZ, maxX, maxY, maxZ));
}


eve::Result<BroadPhaseAabb3D> World3D::queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX,
                                                                float maxY, float maxZ, QueryFilter3D filter) {
    return rigidBackend_->queryAabbBroadPhaseOwned(minX, minY, minZ, maxX, maxY, maxZ, filter);
}


int World3D::querySphere(float x, float y, float z, float radius) {
    return worldValue(rigidBackend_->querySphere(x, y, z, radius));
}


int World3D::queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    return worldValue(rigidBackend_->queryCapsule(ax, ay, az, bx, by, bz, radius));
}


eve::Result<CapsuleOverlap3D> World3D::queryCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                         float radius, QueryFilter3D filter) {
    return rigidBackend_->queryCapsuleOwned(ax, ay, az, bx, by, bz, radius, filter);
}


int World3D::queryBox(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                      float qw) {
    return worldValue(rigidBackend_->queryBox(x, y, z, width, height, depth, qx, qy, qz, qw));
}


int World3D::castSphere(float x, float y, float z, float radius, float dx, float dy, float dz) {
    castSphereAll(x, y, z, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz, int maxHits) {
    return worldValue(rigidBackend_->castSphereAll(x, y, z, radius, dx, dy, dz, maxHits));
}


int World3D::castCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                         float dz) {
    castCapsuleAll(ax, ay, az, bx, by, bz, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                            float dy, float dz, int maxHits) {
    return worldValue(rigidBackend_->castCapsuleAll(ax, ay, az, bx, by, bz, radius, dx, dy, dz, maxHits));
}


int World3D::castBox(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                     float qw, float dx, float dy, float dz) {
    castBoxAll(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castBoxAll(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                        float qw, float dx, float dy, float dz, int maxHits) {
    return worldValue(rigidBackend_->castBoxAll(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz, maxHits));
}


int World3D::closestPoint(float x, float y, float z, float maxDistance) {
    return worldValue(rigidBackend_->closestPoint(x, y, z, maxDistance));
}


}  // namespace eve::physics

namespace eve::physics {
eve::Result<std::vector<eve::WorldSphereContact>> World3D::sweepSphereOwned(double x, double y, double z, double endX,
                                                                            double endY, double endZ,
                                                                            eve::WorldSphereSweepSettings settings) {
    using Output          = eve::Result<std::vector<eve::WorldSphereContact>>;
    const double values[] = {x, y, z, endX, endY, endZ, endX - x, endY - y, endZ - z};
    for (double value : values)
        if (!std::isfinite(value) || std::abs(value) > FLT_MAX)
            return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                          "Sweep coordinates exceed the finite physics float domain",
                                                          "motion"));
    if (!isValid() || !std::isfinite(settings.radius) || settings.radius <= 0 || settings.ignoredBodyId < -1)
        return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                      "Sphere sweep requires a live world and valid radius/filter",
                                                      "worldSweep"));
    world3d_detail::ScopedQueryState3D scoped(*this,
                                              {settings.categoryBits, settings.maskBits, settings.ignoredBodyId, -1});
    auto cast = rigidBackend_->castSphereAll(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                                             settings.radius, static_cast<float>(endX - x),
                                             static_cast<float>(endY - y), static_cast<float>(endZ - z), 257);
    if (!cast) return Output::failure(cast.status());
    if (cast.value() > 256)
        return Output::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "Sphere sweep contact budget exceeded", "contacts"));
    std::vector<eve::WorldSphereContact> output;
    for (int i = 0; i < cast.value(); ++i) {
        // Sweeps report new crossings only. Backends may return an initial overlap with
        // fraction zero and no usable normal; it is not a new impact after a bounce.
        if (getShapeCastResultFraction(i) == 0.0f) continue;
        const int bodyId = getShapeCastResultBodyId(i);
        auto*     body   = findBodyById(bodyId);
        if (!body)
            return Output::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "Sweep returned a stale body", "body"));
        eve::WorldSphereContact contact;
        contact.identity = {runtimeHandle().packed(), body->runtimeHandle().packed()};
        contact.bodyId   = bodyId;
        contact.shapeId  = getShapeCastResultShapeId(i);
        contact.fraction = getShapeCastResultFraction(i);
        contact.x        = x + (endX - x) * contact.fraction;
        contact.y        = y + (endY - y) * contact.fraction;
        contact.z        = z + (endZ - z) * contact.fraction;
        contact.nx       = getShapeCastResultNormalX(i);
        contact.ny       = getShapeCastResultNormalY(i);
        contact.nz       = getShapeCastResultNormalZ(i);
        output.push_back(contact);
    }
    return Output::success(std::move(output));
}
}  // namespace eve::physics
