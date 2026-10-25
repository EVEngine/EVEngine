// eve-hot-path
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/box3d/Box3DWorld.h"
#include "physics/box3d/Box3DWorldInternal.h"
namespace eve::physics::box3d {
using namespace world3d_detail;

void WorldBackend::do_setMoverUp(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.setMoverUp: values must be finite");
    const float lengthSquared = x * x + y * y + z * z;
    if (lengthSquared <= 1e-12f) throw eve::Exception("World3D.setMoverUp: direction must be non-zero");
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    moverUp_                  = b3Vec3{x * inverseLength, y * inverseLength, z * inverseLength};
}

eve::Result<void> WorldBackend::setMoverUp(float x, float y, float z) {
    return checked("physics.world3d.setMoverUp", [&] {
        do_setMoverUp(x, y, z);
        return eve::Result<void>::success();
    });
}

bool WorldBackend::do_moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                                  float dy, float dz) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius > 0.f) || !std::isfinite(radius) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.moveCapsule: all values must be finite; radius > 0");

    owner_->moverDeltaX_ = owner_->moverDeltaY_ = owner_->moverDeltaZ_ = 0.f;
    owner_->moverNormalX_ = owner_->moverNormalY_ = owner_->moverNormalZ_ = 0.f;
    owner_->moverPlaneCount_                                              = 0;
    owner_->moverIterations_                                              = 0;
    owner_->moverGroundDot_                                               = -1.f;
    owner_->moverGrounded_                                                = false;
    if (!isValid()) return false;

    constexpr int planeCapacity = 16;
    struct PlaneCollector {
        WorldBackend    *world = nullptr;
        b3CollisionPlane planes[planeCapacity]{};
        int              count      = 0;
        b3Vec3           desired    = b3Vec3_zero;
        b3Vec3           up         = b3Vec3_axisY;
        b3Vec3           bestNormal = b3Vec3_zero;
        float            bestDot    = 0.f;
        float            maxUpDot   = -1.f;

        static bool callback(b3ShapeId shapeId, const b3PlaneResult *results, int resultCount, void *context) {
            auto *self = static_cast<PlaneCollector *>(context);
            if (b3Shape_IsSensor(shapeId) || self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
            for (int i = 0; i < resultCount && self->count < planeCapacity; ++i) {
                const b3Plane &plane = results[i].plane;
                self->planes[self->count++] =
                    b3CollisionPlane{.plane = plane, .pushLimit = FLT_MAX, .push = 0.f, .clipVelocity = true};
                const float d = b3Dot(plane.normal, self->desired);
                if (d < self->bestDot) {
                    self->bestDot    = d;
                    self->bestNormal = plane.normal;
                }
                self->maxUpDot = std::max(self->maxUpDot, b3Dot(plane.normal, self->up));
            }
            return true;
        }
    } collector;
    collector.world = this;
    collector.up    = moverUp_;

    const b3Pos     start{ax, ay, az};
    b3Pos           current = start;
    const b3Pos     target{ax + dx, ay + dy, az + dz};
    b3Capsule       mover{b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}, radius};
    b3QueryFilter   filter           = do_makeQueryFilter();
    constexpr float toleranceSquared = 1e-8f;
    bool            collided         = false;

    for (int pass = 0; pass < 5; ++pass) {
        collector.count   = 0;
        collector.desired = target - current;
        b3World_CollideMover(worldId_, current, &mover, filter, &PlaneCollector::callback, &collector);
        owner_->moverPlaneCount_ += collector.count;
        collided = collided || collector.count > 0;

        const b3Vec3        targetDelta = target - current;
        b3PlaneSolverResult solved      = b3SolvePlanes(targetDelta, collector.planes, collector.count);
        b3Vec3              delta       = solved.delta;
        struct MoverFilter {
            WorldBackend *world = nullptr;
            static bool   callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<MoverFilter *>(context);
                return !b3Shape_IsSensor(shapeId) && !self->world->do_shouldIgnoreQueryShape(shapeId);
            }
        } moverFilter{this};
        const float fraction =
            b3World_CastMover(worldId_, current, &mover, delta, filter, &MoverFilter::callback, &moverFilter);
        if (fraction < 1.f) collided = true;
        delta                    = fraction * delta;
        current                  = current + delta;
        owner_->moverIterations_ = pass + 1;

        if (b3LengthSquared(delta) <= toleranceSquared) break;
        if (b3LengthSquared(target - current) <= toleranceSquared) break;
    }

    const b3Vec3 total = current - start;
    // Grounding describes support at the destination, not planes left behind.
    struct SupportCollector {
        WorldBackend *world  = nullptr;
        b3Vec3        up     = b3Vec3_axisY;
        b3Vec3        normal = b3Vec3_zero;
        float         upDot  = -1.f;

        static bool callback(b3ShapeId shapeId, const b3PlaneResult *results, int count, void *context) {
            auto *self = static_cast<SupportCollector *>(context);
            if (b3Shape_IsSensor(shapeId) || self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
            for (int i = 0; i < count; ++i) {
                const float upDot = b3Dot(results[i].plane.normal, self->up);
                if (upDot > self->upDot) {
                    self->upDot  = upDot;
                    self->normal = results[i].plane.normal;
                }
            }
            return true;
        }
    } support{this, moverUp_};
    b3World_CollideMover(worldId_, current, &mover, filter, &SupportCollector::callback, &support);
    if (b3LengthSquared(collector.bestNormal) <= toleranceSquared) collector.bestNormal = support.normal;
    owner_->moverDeltaX_    = total.x;
    owner_->moverDeltaY_    = total.y;
    owner_->moverDeltaZ_    = total.z;
    owner_->moverNormalX_   = collector.bestNormal.x;
    owner_->moverNormalY_   = collector.bestNormal.y;
    owner_->moverNormalZ_   = collector.bestNormal.z;
    owner_->moverGroundDot_ = support.upDot;
    owner_->moverGrounded_  = owner_->moverGroundDot_ >= owner_->moverSlopeCos_;
    return collided;
}

eve::Result<bool> WorldBackend::moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius,
                                            float dx, float dy, float dz) {
    return checked("physics.world3d.moveCapsule", [&] {
        return eve::Result<bool>::success(do_moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz));
    });
}

eve::Result<CapsuleMove3D> WorldBackend::do_moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                             float radius, float dx, float dy, float dz,
                                                             QueryFilter3D filter, CapsuleMovePolicy3D policy) {
    if (!isValid())
        return ownedQueryFailure<CapsuleMove3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                                "world");
    const float upLengthSquared = policy.upX * policy.upX + policy.upY * policy.upY + policy.upZ * policy.upZ;
    if (!std::isfinite(policy.upX) || !std::isfinite(policy.upY) || !std::isfinite(policy.upZ) ||
        upLengthSquared <= 1e-12f || !std::isfinite(policy.maxSlopeRadians) || policy.maxSlopeRadians < 0.f ||
        policy.maxSlopeRadians >= 1.57079633f)
        return ownedQueryFailure<CapsuleMove3D>(eve::DiagnosticCode::InvalidArgument, "capsule mover policy is invalid",
                                                "policy");
    const b3Vec3 previousUp       = moverUp_;
    const float  previousSlopeCos = owner_->moverSlopeCos_;
    try {
        ScopedQueryState3D queryState(*owner_, filter);
        const float        inverseUpLength = 1.f / std::sqrt(upLengthSquared);
        moverUp_ = {policy.upX * inverseUpLength, policy.upY * inverseUpLength, policy.upZ * inverseUpLength};
        owner_->moverSlopeCos_ = std::cos(policy.maxSlopeRadians);
        CapsuleMove3D result;
        result.world           = owner_->runtimeHandle_;
        result.constrained     = do_moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz);
        result.grounded        = owner_->isMoverGrounded();
        result.deltaX          = owner_->getMoverDeltaX();
        result.deltaY          = owner_->getMoverDeltaY();
        result.deltaZ          = owner_->getMoverDeltaZ();
        result.normalX         = owner_->getMoverNormalX();
        result.normalY         = owner_->getMoverNormalY();
        result.normalZ         = owner_->getMoverNormalZ();
        result.planeCount      = owner_->getMoverPlaneCount();
        result.iterations      = owner_->getMoverIterations();
        moverUp_               = previousUp;
        owner_->moverSlopeCos_ = previousSlopeCos;
        return eve::Result<CapsuleMove3D>::success(std::move(result));
    } catch (const std::exception &error) {
        moverUp_               = previousUp;
        owner_->moverSlopeCos_ = previousSlopeCos;
        return ownedQueryFailure<CapsuleMove3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "capsuleMove");
    }
}

eve::Result<CapsuleMove3D> WorldBackend::moveCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                          float radius, float dx, float dy, float dz,
                                                          QueryFilter3D filter, CapsuleMovePolicy3D policy) {
    return do_moveCapsuleOwned(ax, ay, az, bx, by, bz, radius, dx, dy, dz, filter, policy);
}

ClothProbeStatus WorldBackend::do_pointProbeFiltered(float x, float y, float z, float radius, ClothContact3D *out,
                                                     uint64_t categoryBits, uint64_t maskBits) const {
    if (out) *out = ClothContact3D{};
    if (!isValid() || radius <= 0.f || !out) return ClothProbeStatus::Miss;

    const b3Vec3 target{x, y, z};
    for (Shape3D *s : owner_->shapes_) {
        if (!s || !s->isValid() || s->isSensor()) continue;
        if ((maskBits & s->getCategoryBits()) == 0 || (s->getMaskBits() & categoryBits) == 0) continue;
        // Box3D closest-point proxies are convex-only. Concave surfaces are handled by
        // swept queries (and rigid contact generation), not b3Shape_GetClosestPoint.
        const std::string kind = s->getKind();
        if (kind == "triangleMesh" || kind == "heightField") continue;
        const b3Vec3 closest = b3Shape_GetClosestPoint(box3d::Access::shapeId(*s), target);
        const b3Vec3 delta   = target - closest;
        const float  d       = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        const float  depth   = radius - d;
        if (depth > 0.f && depth > out->depth) {
            out->hit   = true;
            out->depth = depth;
            if (d > 1e-6f) {
                out->nx = delta.x / d;
                out->ny = delta.y / d;
                out->nz = delta.z / d;
            } else {
                out->nx = 0.f;
                out->ny = 1.f;
                out->nz = 0.f;
            }
            out->body        = s->getBody();
            out->friction    = s->getFriction();
            out->restitution = s->getRestitution();
        }
    }
    return out->hit ? ClothProbeStatus::Hit : ClothProbeStatus::Miss;
}

eve::Result<ClothContact3D> WorldBackend::pointProbeFiltered(float x, float y, float z, float radius,
                                                             uint64_t categoryBits, uint64_t maskBits) const {
    return checked("physics.world3d.pointProbeFiltered", [&] {
        ClothContact3D hit;
        do_pointProbeFiltered(x, y, z, radius, &hit, categoryBits, maskBits);
        return eve::Result<ClothContact3D>::success(hit);
    });
}

}  // namespace eve::physics::box3d
