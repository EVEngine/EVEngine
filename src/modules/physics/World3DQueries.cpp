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
