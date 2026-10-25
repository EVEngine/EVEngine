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

namespace {
int sourceTriangleIndex(b3ShapeId shapeId, int backendIndex) {
    const b3ShapeType type = b3Shape_GetType(shapeId);
    return type == b3_meshShape || type == b3_heightShape ? backendIndex : -1;
}
}  // namespace

bool WorldBackend::do_sphereCast(float x1, float y1, float z1, float x2, float y2, float z2, float radius,
                                 uint64_t maskBits, int ignoredBodyId, CameraSphereHit3D *out) const {
    if (out) *out = CameraSphereHit3D{};
    if (!out || !isValid() || radius < 0.f) return false;

    struct Collector {
        CameraSphereHit3D *out           = nullptr;
        int                ignoredBodyId = -1;
        static float       callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int,
                                    void *context) {
            auto   *self = static_cast<Collector *>(context);
            Body3D *body = bodyFromShape(shapeId);
            if (!body || body->getId() == self->ignoredBodyId) return -1.f;
            if (!self->out->hit || fraction < self->out->fraction) {
                self->out->hit      = true;
                self->out->bodyId   = body->getId();
                self->out->fraction = fraction;
                self->out->x        = static_cast<float>(point.x);
                self->out->y        = static_cast<float>(point.y);
                self->out->z        = static_cast<float>(point.z);
                self->out->nx       = normal.x;
                self->out->ny       = normal.y;
                self->out->nz       = normal.z;
            }
            return fraction;
        }
    } collector{out, ignoredBodyId};

    const b3Vec3       point{0.f, 0.f, 0.f};
    const b3ShapeProxy proxy{&point, 1, radius};
    b3QueryFilter      filter = b3DefaultQueryFilter();
    filter.maskBits           = maskBits;
    b3World_CastShape(worldId_, b3Pos{x1, y1, z1}, &proxy, b3Vec3{x2 - x1, y2 - y1, z2 - z1}, filter,
                      &Collector::callback, &collector);
    return out->hit;
}

eve::Result<CameraSphereHit3D> WorldBackend::sphereCast(float x1, float y1, float z1, float x2, float y2, float z2,
                                                        float radius, uint64_t maskBits, int ignoredBodyId) const {
    return checked("physics.world3d.sphereCast", [&] {
        CameraSphereHit3D hit;
        do_sphereCast(x1, y1, z1, x2, y2, z2, radius, maskBits, ignoredBodyId, &hit);
        return eve::Result<CameraSphereHit3D>::success(hit);
    });
}

int WorldBackend::do_rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2, uint64_t maskBits) {
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits      = maskBits;
    do_rayCastAllInternal(x1, y1, z1, x2, y2, z2, 1, filter);
    return owner_->rayHitBodyId_;
}

eve::Result<int> WorldBackend::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                                               uint64_t maskBits) {
    return checked("physics.world3d.rayCastFiltered",
                   [&] { return eve::Result<int>::success(do_rayCastFiltered(x1, y1, z1, x2, y2, z2, maskBits)); });
}

eve::Result<RayHit3D> WorldBackend::do_rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                                    QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                           "world");
    try {
        ScopedQueryState3D queryState(*owner_, filter);
        RayHit3D           result;
        result.world = owner_->runtimeHandle_;
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z1) || !std::isfinite(x2) ||
            !std::isfinite(y2) || !std::isfinite(z2))
            return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::InvalidArgument, "ray endpoints must be finite",
                                               "ray");
        struct Collector {
            WorldBackend *world  = nullptr;
            RayHit3D     *result = nullptr;
            static float  callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                                   int triangleIndex, int, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->do_shouldIgnoreQueryShape(shapeId)) return -1.f;
                Body3D  *body  = bodyFromShape(shapeId);
                Shape3D *shape = shapeFromShape(shapeId);
                if (!body || !shape || !body->isValid() || !shape->isValid()) return -1.f;
                RayHit3D candidate;
                candidate.hit           = true;
                candidate.world         = self->result->world;
                candidate.body          = body->runtimeHandle();
                candidate.shape         = shape->runtimeHandle();
                candidate.bodyId        = body->getId();
                candidate.shapeId       = shape->getId();
                candidate.shapeTag      = shape->getTag();
                candidate.materialId    = static_cast<int>(static_cast<std::uint32_t>(materialId));
                candidate.triangleIndex = sourceTriangleIndex(shapeId, triangleIndex);
                candidate.x             = static_cast<float>(point.x);
                candidate.y             = static_cast<float>(point.y);
                candidate.z             = static_cast<float>(point.z);
                candidate.normalX       = normal.x;
                candidate.normalY       = normal.y;
                candidate.normalZ       = normal.z;
                candidate.fraction      = fraction;
                if (!self->result->hit || fraction < self->result->fraction ||
                    (fraction == self->result->fraction && candidate.shapeId < self->result->shapeId))
                    *self->result = candidate;
                return self->result->fraction;
            }
        } collector{this, &result};
        b3World_CastRay(worldId_, b3Pos{x1, y1, z1}, b3Vec3{x2 - x1, y2 - y1, z2 - z1}, do_makeQueryFilter(),
                        &Collector::callback, &collector);
        if (!result.hit) return eve::Result<RayHit3D>::success(std::move(result));
        if (result.body.isInvalid() || result.shape.isInvalid())
            return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::InvariantViolation,
                                               "ray hit could not be mapped to live generation-qualified handles",
                                               "hit");
        return eve::Result<RayHit3D>::success(std::move(result));
    } catch (const std::exception &error) {
        return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "ray");
    }
}

eve::Result<RayHit3D> WorldBackend::rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                                 QueryFilter3D filter) {
    return do_rayCastOwned(x1, y1, z1, x2, y2, z2, filter);
}

int WorldBackend::do_rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) {
    return do_rayCastAllInternal(x1, y1, z1, x2, y2, z2, maxHits, do_makeQueryFilter());
}

eve::Result<int> WorldBackend::rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) {
    return checked("physics.world3d.rayCastAll",
                   [&] { return eve::Result<int>::success(do_rayCastAll(x1, y1, z1, x2, y2, z2, maxHits)); });
}

int WorldBackend::do_rayCastAllInternal(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits,
                                        b3QueryFilter filter) {
    owner_->rayHitBodyId_        = -1;
    owner_->rayHitShapeId_       = -1;
    owner_->rayHitShapeTag_      = 0;
    owner_->rayHitMaterialId_    = 0;
    owner_->rayHitTriangleIndex_ = -1;
    owner_->rayHitX_             = 0.f;
    owner_->rayHitY_             = 0.f;
    owner_->rayHitZ_             = 0.f;
    owner_->rayHitNormalX_       = 0.f;
    owner_->rayHitNormalY_       = 0.f;
    owner_->rayHitNormalZ_       = 0.f;
    owner_->rayHitFraction_      = 0.f;
    owner_->rayResults_.clear();
    if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z1) || !std::isfinite(x2) || !std::isfinite(y2) ||
        !std::isfinite(z2))
        throw eve::Exception("World3D.rayCastAll: endpoints must be finite");
    constexpr int maxRayResults = 4096;
    if (maxHits < 1 || maxHits > maxRayResults)
        throw eve::Exception("World3D.rayCastAll: maxHits must be in [1, 4096]");
    if (!isValid()) return 0;

    b3Pos  origin{x1, y1, z1};
    b3Vec3 translation{x2 - x1, y2 - y1, z2 - z1};
    struct RayCollector {
        WorldBackend *world    = nullptr;
        int           capacity = 0;

        static bool better(const RayResult &a, const RayResult &b) {
            return a.fraction < b.fraction || (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->owner_->rayResults_.size()) < capacity) return 1.f;
            const auto worst =
                std::max_element(world->owner_->rayResults_.begin(), world->owner_->rayResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                              int triangleIndex, int, void *context) {
            auto *self = static_cast<RayCollector *>(context);
            if (self->world->do_shouldIgnoreQueryShape(shapeId)) return -1.f;
            Body3D  *body  = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return -1.f;

            RayResult candidate{body->getId(),
                                shape->getId(),
                                shape->getTag(),
                                static_cast<int>(static_cast<uint32_t>(materialId)),
                                sourceTriangleIndex(shapeId, triangleIndex),
                                static_cast<float>(point.x),
                                static_cast<float>(point.y),
                                static_cast<float>(point.z),
                                normal.x,
                                normal.y,
                                normal.z,
                                fraction};
            auto     &results = self->world->owner_->rayResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    owner_->rayResults_.reserve(static_cast<size_t>(maxHits));
    b3World_CastRay(worldId_, origin, translation, filter, &RayCollector::callback, &collector);
    std::sort(owner_->rayResults_.begin(), owner_->rayResults_.end(), RayCollector::better);
    if (!owner_->rayResults_.empty()) {
        const RayResult &hit         = owner_->rayResults_.front();
        owner_->rayHitBodyId_        = hit.bodyId;
        owner_->rayHitShapeId_       = hit.shapeId;
        owner_->rayHitShapeTag_      = hit.shapeTag;
        owner_->rayHitMaterialId_    = hit.materialId;
        owner_->rayHitTriangleIndex_ = hit.triangleIndex;
        owner_->rayHitX_             = hit.x;
        owner_->rayHitY_             = hit.y;
        owner_->rayHitZ_             = hit.z;
        owner_->rayHitNormalX_       = hit.normalX;
        owner_->rayHitNormalY_       = hit.normalY;
        owner_->rayHitNormalZ_       = hit.normalZ;
        owner_->rayHitFraction_      = hit.fraction;
    }
    return static_cast<int>(owner_->rayResults_.size());
}

int WorldBackend::do_queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    owner_->queryBodyIds_.clear();
    owner_->queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        WorldBackend                     *world   = nullptr;
        std::vector<int>                 *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes  = nullptr;
        std::unordered_set<int>           seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
            Body3D  *b     = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!b || !shape) return true;
            int id = b->getId();
            if (self->seen.insert(id).second) self->bodyIds->push_back(id);
            self->shapes->emplace_back(shape->getId(), shape->getTag());
            return true;
        }
    } cb;
    cb.world   = this;
    cb.bodyIds = &owner_->queryBodyIds_;
    cb.shapes  = &owner_->queryShapes_;

    b3AABB aabb;
    aabb.lowerBound      = b3Vec3{std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    aabb.upperBound      = b3Vec3{std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    b3QueryFilter filter = do_makeQueryFilter();
    b3World_OverlapAABB(worldId_, aabb, filter, &Collector::callback, &cb);
    std::sort(owner_->queryBodyIds_.begin(), owner_->queryBodyIds_.end());
    std::sort(owner_->queryShapes_.begin(), owner_->queryShapes_.end());
    return static_cast<int>(owner_->queryBodyIds_.size());
}

eve::Result<int> WorldBackend::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    return checked("physics.world3d.queryAABB",
                   [&] { return eve::Result<int>::success(do_queryAABB(minX, minY, minZ, maxX, maxY, maxZ)); });
}

eve::Result<BroadPhaseAabb3D> WorldBackend::do_queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX,
                                                                        float maxY, float maxZ, QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                                   "world");
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ) || !std::isfinite(maxX) ||
        !std::isfinite(maxY) || !std::isfinite(maxZ))
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::InvalidArgument,
                                                   "broad-phase AABB must be finite", "aabb");
    try {
        ScopedQueryState3D queryState(*owner_, filter);
        BroadPhaseAabb3D   result;
        struct Collector {
            WorldBackend     *world  = nullptr;
            BroadPhaseAabb3D *result = nullptr;

            static bool less(const BroadPhaseHit3D &lhs, const BroadPhaseHit3D &rhs) noexcept {
                return lhs.bodyId != rhs.bodyId ? lhs.bodyId < rhs.bodyId : lhs.shapeId < rhs.shapeId;
            }

            static bool callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
                Body3D  *body  = bodyFromShape(shapeId);
                Shape3D *shape = shapeFromShape(shapeId);
                if (!body || !shape || !body->isValid() || !shape->isValid()) return true;
                BroadPhaseHit3D hit{body->runtimeHandle(), shape->runtimeHandle(), body->getId(),
                                    shape->getId(),        shape->getTag(),        shape->getMaterialId()};
                auto           &output = *self->result;
                if (output.count < BroadPhaseAabb3D::Capacity) {
                    output.hits[output.count++] = hit;
                } else {
                    output.truncated = true;
                    auto worst       = std::max_element(output.hits.begin(), output.hits.end(), less);
                    if (less(hit, *worst)) *worst = hit;
                }
                return true;
            }
        } collector{this, &result};
        b3AABB bounds;
        bounds.lowerBound = {std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
        bounds.upperBound = {std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
        b3World_OverlapAABB(worldId_, bounds, do_makeQueryFilter(), &Collector::callback, &collector);
        std::sort(result.hits.begin(), result.hits.begin() + static_cast<std::ptrdiff_t>(result.count),
                  Collector::less);
        return eve::Result<BroadPhaseAabb3D>::success(std::move(result));
    } catch (const std::exception &error) {
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "aabb");
    }
}

eve::Result<BroadPhaseAabb3D> WorldBackend::queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX,
                                                                     float maxY, float maxZ, QueryFilter3D filter) {
    return do_queryAabbBroadPhaseOwned(minX, minY, minZ, maxX, maxY, maxZ, filter);
}

int WorldBackend::do_overlapProxy(b3Pos origin, const b3ShapeProxy &proxy) {
    owner_->queryBodyIds_.clear();
    owner_->queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        WorldBackend                     *world   = nullptr;
        std::vector<int>                 *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes  = nullptr;
        std::unordered_set<int>           seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
            Body3D  *body  = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (body && shape) {
                if (self->seen.insert(body->getId()).second) self->bodyIds->push_back(body->getId());
                self->shapes->emplace_back(shape->getId(), shape->getTag());
            }
            return true;
        }
    } collector;
    collector.world      = this;
    collector.bodyIds    = &owner_->queryBodyIds_;
    collector.shapes     = &owner_->queryShapes_;
    b3QueryFilter filter = do_makeQueryFilter();
    b3World_OverlapShape(worldId_, origin, &proxy, filter, &Collector::callback, &collector);
    // Broad-phase traversal order is unspecified; stable ids make script behavior deterministic.
    std::sort(owner_->queryBodyIds_.begin(), owner_->queryBodyIds_.end());
    std::sort(owner_->queryShapes_.begin(), owner_->queryShapes_.end());
    return static_cast<int>(owner_->queryBodyIds_.size());
}

int WorldBackend::do_querySphere(float x, float y, float z, float radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("World3D.querySphere: coordinates and radius must be finite; radius >= 0");
    const b3Vec3       point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return do_overlapProxy(b3Pos{x, y, z}, proxy);
}

eve::Result<int> WorldBackend::querySphere(float x, float y, float z, float radius) {
    return checked("physics.world3d.querySphere",
                   [&] { return eve::Result<int>::success(do_querySphere(x, y, z, radius)); });
}

int WorldBackend::do_queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("World3D.queryCapsule: coordinates and radius must be finite; radius >= 0");
    // Relative points preserve Box3D's large-world precision.
    const b3Vec3       points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return do_overlapProxy(b3Pos{ax, ay, az}, proxy);
}

eve::Result<int> WorldBackend::queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    return checked("physics.world3d.queryCapsule",
                   [&] { return eve::Result<int>::success(do_queryCapsule(ax, ay, az, bx, by, bz, radius)); });
}

eve::Result<CapsuleOverlap3D> WorldBackend::do_queryCapsuleOwned(float ax, float ay, float az, float bx, float by,
                                                                 float bz, float radius, QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                                   "world");
    try {
        ScopedQueryState3D queryState(*owner_, filter);
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
            !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(radius) || radius < 0.f)
            return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::InvalidArgument,
                                                       "capsule coordinates and radius are invalid", "capsule");
        CapsuleOverlap3D result;
        result.world = owner_->runtimeHandle_;
        struct Collector {
            WorldBackend        *world = nullptr;
            std::array<int, 256> bodyIds{};
            std::size_t          count = 0;
            static bool          callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
                Body3D *body = bodyFromShape(shapeId);
                if (!body || !body->isValid()) return true;
                const int bodyId = body->getId();
                if (std::find(self->bodyIds.begin(), self->bodyIds.begin() + static_cast<std::ptrdiff_t>(self->count),
                                       bodyId) != self->bodyIds.begin() + static_cast<std::ptrdiff_t>(self->count))
                    return true;
                if (self->count < self->bodyIds.size()) self->bodyIds[self->count++] = bodyId;
                return true;
            }
        } collector{this};
        const b3Vec3       points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
        const b3ShapeProxy proxy{points, 2, radius};
        b3World_OverlapShape(worldId_, b3Pos{ax, ay, az}, &proxy, do_makeQueryFilter(), &Collector::callback,
                             &collector);
        result.bodyCount = static_cast<int>(collector.count);
        return eve::Result<CapsuleOverlap3D>::success(std::move(result));
    } catch (const std::exception &error) {
        return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "capsule");
    }
}

eve::Result<CapsuleOverlap3D> WorldBackend::queryCapsuleOwned(float ax, float ay, float az, float bx, float by,
                                                              float bz, float radius, QueryFilter3D filter) {
    return do_queryCapsuleOwned(ax, ay, az, bx, by, bz, radius, filter);
}

int WorldBackend::do_queryBox(float x, float y, float z, float width, float height, float depth, float qx, float qy,
                              float qz, float qw) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.queryBox: center must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.queryBox");
    b3Vec3       points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.queryBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return do_overlapProxy(b3Pos{x, y, z}, proxy);
}

eve::Result<int> WorldBackend::queryBox(float x, float y, float z, float width, float height, float depth, float qx,
                                        float qy, float qz, float qw) {
    return checked("physics.world3d.queryBox", [&] {
        return eve::Result<int>::success(do_queryBox(x, y, z, width, height, depth, qx, qy, qz, qw));
    });
}

int WorldBackend::do_castProxyAll(b3Pos origin, const b3ShapeProxy &proxy, b3Vec3 translation, int maxHits) {
    owner_->shapeCastBodyId_        = -1;
    owner_->shapeCastShapeId_       = -1;
    owner_->shapeCastShapeTag_      = 0;
    owner_->shapeCastMaterialId_    = 0;
    owner_->shapeCastTriangleIndex_ = -1;
    owner_->shapeCastX_ = owner_->shapeCastY_ = owner_->shapeCastZ_ = 0.f;
    owner_->shapeCastNormalX_ = owner_->shapeCastNormalY_ = owner_->shapeCastNormalZ_ = 0.f;
    owner_->shapeCastFraction_                                                        = 0.f;
    owner_->shapeCastResults_.clear();
    constexpr int maxShapeCastResults = 4096;
    if (maxHits < 1 || maxHits > maxShapeCastResults)
        throw eve::Exception("World3D Shape Cast: maxHits must be in [1, 4096]");
    if (!isValid()) return 0;

    struct CastCollector {
        WorldBackend *world    = nullptr;
        int           capacity = 0;

        static bool better(const ShapeCastResult &a, const ShapeCastResult &b) {
            return a.fraction < b.fraction || (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->owner_->shapeCastResults_.size()) < capacity) return 1.f;
            const auto worst = std::max_element(world->owner_->shapeCastResults_.begin(),
                                                world->owner_->shapeCastResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                              int triangleIndex, int, void *context) {
            auto *self = static_cast<CastCollector *>(context);
            if (self->world->do_shouldIgnoreQueryShape(shapeId)) return -1.f;
            Body3D  *body  = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return -1.f;

            ShapeCastResult candidate{body->getId(),
                                      shape->getId(),
                                      shape->getTag(),
                                      static_cast<int>(static_cast<uint32_t>(materialId)),
                                      sourceTriangleIndex(shapeId, triangleIndex),
                                      static_cast<float>(point.x),
                                      static_cast<float>(point.y),
                                      static_cast<float>(point.z),
                                      normal.x,
                                      normal.y,
                                      normal.z,
                                      fraction};
            auto           &results = self->world->owner_->shapeCastResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    owner_->shapeCastResults_.reserve(static_cast<size_t>(maxHits));
    b3QueryFilter filter = do_makeQueryFilter();
    b3World_CastShape(worldId_, origin, &proxy, translation, filter, &CastCollector::callback, &collector);
    std::sort(owner_->shapeCastResults_.begin(), owner_->shapeCastResults_.end(), CastCollector::better);
    if (!owner_->shapeCastResults_.empty()) {
        const ShapeCastResult &hit      = owner_->shapeCastResults_.front();
        owner_->shapeCastBodyId_        = hit.bodyId;
        owner_->shapeCastShapeId_       = hit.shapeId;
        owner_->shapeCastShapeTag_      = hit.shapeTag;
        owner_->shapeCastMaterialId_    = hit.materialId;
        owner_->shapeCastTriangleIndex_ = hit.triangleIndex;
        owner_->shapeCastX_             = hit.x;
        owner_->shapeCastY_             = hit.y;
        owner_->shapeCastZ_             = hit.z;
        owner_->shapeCastNormalX_       = hit.normalX;
        owner_->shapeCastNormalY_       = hit.normalY;
        owner_->shapeCastNormalZ_       = hit.normalZ;
        owner_->shapeCastFraction_      = hit.fraction;
    }
    return static_cast<int>(owner_->shapeCastResults_.size());
}

int WorldBackend::do_castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) || !std::isfinite(radius) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.castSphere: all values must be finite; radius >= 0");
    const b3Vec3       point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return do_castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

eve::Result<int> WorldBackend::castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz,
                                             int maxHits) {
    return checked("physics.world3d.castSphereAll",
                   [&] { return eve::Result<int>::success(do_castSphereAll(x, y, z, radius, dx, dy, dz, maxHits)); });
}

int WorldBackend::do_castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                                    float dy, float dz, int maxHits) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.castCapsule: all values must be finite; radius >= 0");
    const b3Vec3       points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return do_castProxyAll(b3Pos{ax, ay, az}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

eve::Result<int> WorldBackend::castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius,
                                              float dx, float dy, float dz, int maxHits) {
    return checked("physics.world3d.castCapsuleAll", [&] {
        return eve::Result<int>::success(do_castCapsuleAll(ax, ay, az, bx, by, bz, radius, dx, dy, dz, maxHits));
    });
}

int WorldBackend::do_castBoxAll(float x, float y, float z, float width, float height, float depth, float qx, float qy,
                                float qz, float qw, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.castBox: center and translation must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.castBox");
    b3Vec3       points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.castBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return do_castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

eve::Result<int> WorldBackend::castBoxAll(float x, float y, float z, float width, float height, float depth, float qx,
                                          float qy, float qz, float qw, float dx, float dy, float dz, int maxHits) {
    return checked("physics.world3d.castBoxAll", [&] {
        return eve::Result<int>::success(
            do_castBoxAll(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz, maxHits));
    });
}

int WorldBackend::do_closestPoint(float x, float y, float z, float maxDistance) {
    owner_->closestBodyId_   = -1;
    owner_->closestShapeId_  = -1;
    owner_->closestShapeTag_ = 0;
    owner_->closestX_ = owner_->closestY_ = owner_->closestZ_ = 0.f;
    owner_->closestNormalX_ = owner_->closestNormalY_ = owner_->closestNormalZ_ = 0.f;
    owner_->closestDistance_                                                    = 0.f;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(maxDistance >= 0.f) ||
        !std::isfinite(maxDistance))
        throw eve::Exception("World3D.closestPoint: target must be finite; maxDistance finite and >= 0");
    constexpr double floatLimit = static_cast<double>(FLT_MAX);
    const double     distance   = static_cast<double>(maxDistance);
    if (distance * distance > floatLimit || std::fabs(static_cast<double>(x)) + distance > floatLimit ||
        std::fabs(static_cast<double>(y)) + distance > floatLimit ||
        std::fabs(static_cast<double>(z)) + distance > floatLimit)
        throw eve::Exception("World3D.closestPoint: search bounds are too large");
    if (!isValid()) return -1;

    struct ClosestCollector {
        WorldBackend *world               = nullptr;
        b3Vec3        target              = b3Vec3_zero;
        float         bestDistanceSquared = 0.f;
        int           bodyId              = -1;
        int           stableShapeId       = -1;
        int           shapeTag            = 0;
        b3Vec3        point               = b3Vec3_zero;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<ClosestCollector *>(context);
            if (self->world->do_shouldIgnoreQueryShape(shapeId)) return true;
            Body3D  *body  = bodyFromShape(shapeId);
            Shape3D *shape = shapeFromShape(shapeId);
            if (!body || !shape) return true;
            const b3Vec3    point           = b3Shape_GetClosestPoint(shapeId, self->target);
            const b3Vec3    delta           = self->target - point;
            const float     distanceSquared = b3LengthSquared(delta);
            const int       bodyId          = body->getId();
            const int       stableShapeId   = shape->getId();
            constexpr float tieTolerance    = 1e-7f;
            if (distanceSquared < self->bestDistanceSquared - tieTolerance ||
                (std::fabs(distanceSquared - self->bestDistanceSquared) <= tieTolerance &&
                 (self->bodyId < 0 || bodyId < self->bodyId ||
                  (bodyId == self->bodyId && stableShapeId < self->stableShapeId)))) {
                self->bestDistanceSquared = distanceSquared;
                self->bodyId              = bodyId;
                self->stableShapeId       = stableShapeId;
                self->shapeTag            = shape->getTag();
                self->point               = point;
            }
            return true;
        }
    } collector;
    collector.world               = this;
    collector.target              = b3Vec3{x, y, z};
    collector.bestDistanceSquared = maxDistance * maxDistance;

    const b3Vec3        extent{maxDistance, maxDistance, maxDistance};
    const b3AABB        bounds{collector.target - extent, collector.target + extent};
    const b3QueryFilter filter = do_makeQueryFilter();
    b3World_OverlapAABB(worldId_, bounds, filter, &ClosestCollector::callback, &collector);
    if (collector.bodyId < 0) return -1;

    owner_->closestBodyId_   = collector.bodyId;
    owner_->closestShapeId_  = collector.stableShapeId;
    owner_->closestShapeTag_ = collector.shapeTag;
    owner_->closestX_        = collector.point.x;
    owner_->closestY_        = collector.point.y;
    owner_->closestZ_        = collector.point.z;
    owner_->closestDistance_ = std::sqrt(std::max(0.f, collector.bestDistanceSquared));
    if (owner_->closestDistance_ > 1e-6f) {
        const float inverseDistance = 1.f / owner_->closestDistance_;
        owner_->closestNormalX_     = (x - owner_->closestX_) * inverseDistance;
        owner_->closestNormalY_     = (y - owner_->closestY_) * inverseDistance;
        owner_->closestNormalZ_     = (z - owner_->closestZ_) * inverseDistance;
    }
    return owner_->closestBodyId_;
}

eve::Result<int> WorldBackend::closestPoint(float x, float y, float z, float maxDistance) {
    return checked("physics.world3d.closestPoint",
                   [&] { return eve::Result<int>::success(do_closestPoint(x, y, z, maxDistance)); });
}

}  // namespace eve::physics::box3d
