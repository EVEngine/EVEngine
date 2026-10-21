#include "physics/World3D.h"

#include "common/Exception.h"

#include <box3d/box3d.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace eve::physics {

void World3D::setMoverUp(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.setMoverUp: values must be finite");
    const float lengthSquared = x * x + y * y + z * z;
    if (lengthSquared <= 1e-12f) throw eve::Exception("World3D.setMoverUp: direction must be non-zero");
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    moverUp_                  = b3Vec3{x * inverseLength, y * inverseLength, z * inverseLength};
}

void World3D::setMoverSlopeLimit(float degrees) {
    if (!std::isfinite(degrees)) throw eve::Exception("World3D.setMoverSlopeLimit: degrees must be finite");
    degrees        = std::clamp(degrees, 0.f, 89.9f);
    moverSlopeCos_ = std::cos(degrees * 0.01745329251994329577f);
}

bool World3D::moveCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                          float dz) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius > 0.f) || !std::isfinite(radius) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.moveCapsule: all values must be finite; radius > 0");

    moverDeltaX_ = moverDeltaY_ = moverDeltaZ_ = 0.f;
    moverNormalX_ = moverNormalY_ = moverNormalZ_ = 0.f;
    moverPlaneCount_                              = 0;
    moverIterations_                              = 0;
    moverGroundDot_                               = -1.f;
    moverGrounded_                                = false;
    if (!isValid()) return false;

    constexpr int planeCapacity = 16;
    struct PlaneCollector {
        World3D         *world = nullptr;
        b3CollisionPlane planes[planeCapacity]{};
        int              count      = 0;
        b3Vec3           desired    = b3Vec3_zero;
        b3Vec3           up         = b3Vec3_axisY;
        b3Vec3           bestNormal = b3Vec3_zero;
        float            bestDot    = 0.f;
        float            maxUpDot   = -1.f;

        static bool callback(b3ShapeId shapeId, const b3PlaneResult *results, int resultCount, void *context) {
            auto *self = static_cast<PlaneCollector *>(context);
            if (b3Shape_IsSensor(shapeId) || self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
    b3QueryFilter   filter           = makeQueryFilter();
    constexpr float toleranceSquared = 1e-8f;
    bool            collided         = false;

    for (int pass = 0; pass < 5; ++pass) {
        collector.count   = 0;
        collector.desired = target - current;
        b3World_CollideMover(worldId_, current, &mover, filter, &PlaneCollector::callback, &collector);
        moverPlaneCount_ += collector.count;
        collided = collided || collector.count > 0;

        const b3Vec3        targetDelta = target - current;
        b3PlaneSolverResult solved      = b3SolvePlanes(targetDelta, collector.planes, collector.count);
        b3Vec3              delta       = solved.delta;
        struct MoverFilter {
            World3D    *world = nullptr;
            static bool callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<MoverFilter *>(context);
                return !b3Shape_IsSensor(shapeId) && !self->world->shouldIgnoreQueryShape(shapeId);
            }
        } moverFilter{this};
        const float fraction =
            b3World_CastMover(worldId_, current, &mover, delta, filter, &MoverFilter::callback, &moverFilter);
        if (fraction < 1.f) collided = true;
        delta            = fraction * delta;
        current          = current + delta;
        moverIterations_ = pass + 1;

        if (b3LengthSquared(delta) <= toleranceSquared) break;
        if (b3LengthSquared(target - current) <= toleranceSquared) break;
    }

    const b3Vec3 total = current - start;
    // Solver planes describe every point visited along the path. Grounding
    // describes support at the resolved destination, never a floor left behind.
    struct SupportCollector {
        World3D *world  = nullptr;
        b3Vec3   up     = b3Vec3_axisY;
        b3Vec3   normal = b3Vec3_zero;
        float    upDot  = -1.f;

        static bool callback(b3ShapeId shapeId, const b3PlaneResult *results, int count, void *context) {
            auto *self = static_cast<SupportCollector *>(context);
            if (b3Shape_IsSensor(shapeId) || self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
    // Keep a representative blocking normal for wall sliding. With purely
    // tangential motion there is no blocking plane, so return the support normal.
    if (b3LengthSquared(collector.bestNormal) <= toleranceSquared) collector.bestNormal = support.normal;
    moverDeltaX_    = total.x;
    moverDeltaY_    = total.y;
    moverDeltaZ_    = total.z;
    moverNormalX_   = collector.bestNormal.x;
    moverNormalY_   = collector.bestNormal.y;
    moverNormalZ_   = collector.bestNormal.z;
    moverGroundDot_ = support.upDot;
    moverGrounded_  = moverGroundDot_ >= moverSlopeCos_;
    return collided;
}

}  // namespace eve::physics
