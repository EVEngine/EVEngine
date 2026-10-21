#include "physics/World3DQueryInternal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace eve::physics {
using namespace query_detail;
namespace {
int sourceTriangleIndex(b3ShapeId shape, int backendIndex) {
    const b3ShapeType type = b3Shape_GetType(shape);
    return type == b3_meshShape || type == b3_heightShape ? backendIndex : -1;
}
}  // namespace

int World3D::rayCast(float x1, float y1, float z1, float x2, float y2, float z2) {
    rayCastAll(x1, y1, z1, x2, y2, z2, 1);
    return rayHitBodyId_;
}

int World3D::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2, uint64_t maskBits) {
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.maskBits      = maskBits;
    rayCastAllInternal(x1, y1, z1, x2, y2, z2, 1, filter);
    return rayHitBodyId_;
}

eve::Result<RayHit3D> World3D::rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                            QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                           "world");
    try {
        ScopedQueryState3D queryState(*this, filter);
        RayHit3D           result;
        result.world = runtimeHandle_;
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(z1) || !std::isfinite(x2) ||
            !std::isfinite(y2) || !std::isfinite(z2))
            return ownedQueryFailure<RayHit3D>(eve::DiagnosticCode::InvalidArgument, "ray endpoints must be finite",
                                               "ray");
        struct Collector {
            World3D     *world  = nullptr;
            RayHit3D    *result = nullptr;
            static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                                  int triangleIndex, int, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->shouldIgnoreQueryShape(shapeId)) return -1.f;
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
        b3World_CastRay(worldId_, b3Pos{x1, y1, z1}, b3Vec3{x2 - x1, y2 - y1, z2 - z1}, makeQueryFilter(),
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

int World3D::rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) {
    return rayCastAllInternal(x1, y1, z1, x2, y2, z2, maxHits, makeQueryFilter());
}

int World3D::rayCastAllInternal(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits,
                                b3QueryFilter filter) {
    rayHitBodyId_        = -1;
    rayHitShapeId_       = -1;
    rayHitShapeTag_      = 0;
    rayHitMaterialId_    = 0;
    rayHitTriangleIndex_ = -1;
    rayHitX_             = 0.f;
    rayHitY_             = 0.f;
    rayHitZ_             = 0.f;
    rayHitNormalX_       = 0.f;
    rayHitNormalY_       = 0.f;
    rayHitNormalZ_       = 0.f;
    rayHitFraction_      = 0.f;
    rayResults_.clear();
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
        World3D *world    = nullptr;
        int      capacity = 0;

        static bool better(const RayResult &a, const RayResult &b) {
            return a.fraction < b.fraction || (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->rayResults_.size()) < capacity) return 1.f;
            const auto worst = std::max_element(world->rayResults_.begin(), world->rayResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                              int triangleIndex, int, void *context) {
            auto *self = static_cast<RayCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return -1.f;
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
            auto     &results = self->world->rayResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    rayResults_.reserve(static_cast<size_t>(maxHits));
    b3World_CastRay(worldId_, origin, translation, filter, &RayCollector::callback, &collector);
    std::sort(rayResults_.begin(), rayResults_.end(), RayCollector::better);
    if (!rayResults_.empty()) {
        const RayResult &hit = rayResults_.front();
        rayHitBodyId_        = hit.bodyId;
        rayHitShapeId_       = hit.shapeId;
        rayHitShapeTag_      = hit.shapeTag;
        rayHitMaterialId_    = hit.materialId;
        rayHitTriangleIndex_ = hit.triangleIndex;
        rayHitX_             = hit.x;
        rayHitY_             = hit.y;
        rayHitZ_             = hit.z;
        rayHitNormalX_       = hit.normalX;
        rayHitNormalY_       = hit.normalY;
        rayHitNormalZ_       = hit.normalZ;
        rayHitFraction_      = hit.fraction;
    }
    return static_cast<int>(rayResults_.size());
}

int World3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    queryBodyIds_.clear();
    queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        World3D                          *world   = nullptr;
        std::vector<int>                 *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes  = nullptr;
        std::unordered_set<int>           seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
    cb.bodyIds = &queryBodyIds_;
    cb.shapes  = &queryShapes_;

    b3AABB aabb;
    aabb.lowerBound      = b3Vec3{std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    aabb.upperBound      = b3Vec3{std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapAABB(worldId_, aabb, filter, &Collector::callback, &cb);
    std::sort(queryBodyIds_.begin(), queryBodyIds_.end());
    std::sort(queryShapes_.begin(), queryShapes_.end());
    return static_cast<int>(queryBodyIds_.size());
}

eve::Result<BroadPhaseAabb3D> World3D::queryAabbBroadPhaseOwned(float minX, float minY, float minZ, float maxX,
                                                                float maxY, float maxZ, QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                                   "world");
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ) || !std::isfinite(maxX) ||
        !std::isfinite(maxY) || !std::isfinite(maxZ))
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::InvalidArgument,
                                                   "broad-phase AABB must be finite", "aabb");
    try {
        ScopedQueryState3D queryState(*this, filter);
        BroadPhaseAabb3D   result;
        struct Collector {
            World3D          *world  = nullptr;
            BroadPhaseAabb3D *result = nullptr;

            static bool less(const BroadPhaseHit3D &lhs, const BroadPhaseHit3D &rhs) noexcept {
                return lhs.bodyId != rhs.bodyId ? lhs.bodyId < rhs.bodyId : lhs.shapeId < rhs.shapeId;
            }

            static bool callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
        b3World_OverlapAABB(worldId_, bounds, makeQueryFilter(), &Collector::callback, &collector);
        std::sort(result.hits.begin(), result.hits.begin() + static_cast<std::ptrdiff_t>(result.count),
                  Collector::less);
        return eve::Result<BroadPhaseAabb3D>::success(std::move(result));
    } catch (const std::exception &error) {
        return ownedQueryFailure<BroadPhaseAabb3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "aabb");
    }
}

int World3D::overlapProxy(b3Pos origin, const b3ShapeProxy &proxy) {
    queryBodyIds_.clear();
    queryShapes_.clear();
    if (!isValid()) return 0;

    struct Collector {
        World3D                          *world   = nullptr;
        std::vector<int>                 *bodyIds = nullptr;
        std::vector<std::pair<int, int>> *shapes  = nullptr;
        std::unordered_set<int>           seen;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<Collector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
    collector.bodyIds    = &queryBodyIds_;
    collector.shapes     = &queryShapes_;
    b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapShape(worldId_, origin, &proxy, filter, &Collector::callback, &collector);
    // Broad-phase traversal order is unspecified; stable ids make script behavior deterministic.
    std::sort(queryBodyIds_.begin(), queryBodyIds_.end());
    std::sort(queryShapes_.begin(), queryShapes_.end());
    return static_cast<int>(queryBodyIds_.size());
}

int World3D::querySphere(float x, float y, float z, float radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("World3D.querySphere: coordinates and radius must be finite; radius >= 0");
    const b3Vec3       point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return overlapProxy(b3Pos{x, y, z}, proxy);
}

int World3D::queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius))
        throw eve::Exception("World3D.queryCapsule: coordinates and radius must be finite; radius >= 0");
    // Relative points preserve Box3D's large-world precision.
    const b3Vec3       points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return overlapProxy(b3Pos{ax, ay, az}, proxy);
}

eve::Result<CapsuleOverlap3D> World3D::queryCapsuleOwned(float ax, float ay, float az, float bx, float by, float bz,
                                                         float radius, QueryFilter3D filter) {
    if (!isValid())
        return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::StaleHandle, "physics world is no longer valid",
                                                   "world");
    try {
        ScopedQueryState3D queryState(*this, filter);
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) ||
            !std::isfinite(by) || !std::isfinite(bz) || !std::isfinite(radius) || radius < 0.f)
            return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::InvalidArgument,
                                                       "capsule coordinates and radius are invalid", "capsule");
        CapsuleOverlap3D result;
        result.world = runtimeHandle_;
        struct Collector {
            World3D             *world = nullptr;
            std::array<int, 256> bodyIds{};
            std::size_t          count = 0;
            static bool          callback(b3ShapeId shapeId, void *context) {
                auto *self = static_cast<Collector *>(context);
                if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
        b3World_OverlapShape(worldId_, b3Pos{ax, ay, az}, &proxy, makeQueryFilter(), &Collector::callback, &collector);
        result.bodyCount = static_cast<int>(collector.count);
        return eve::Result<CapsuleOverlap3D>::success(std::move(result));
    } catch (const std::exception &error) {
        return ownedQueryFailure<CapsuleOverlap3D>(eve::DiagnosticCode::InvalidArgument, error.what(), "capsule");
    }
}

int World3D::queryBox(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                      float qw) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.queryBox: center must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.queryBox");
    b3Vec3       points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.queryBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return overlapProxy(b3Pos{x, y, z}, proxy);
}

int World3D::castProxyAll(b3Pos origin, const b3ShapeProxy &proxy, b3Vec3 translation, int maxHits) {
    shapeCastBodyId_        = -1;
    shapeCastShapeId_       = -1;
    shapeCastShapeTag_      = 0;
    shapeCastMaterialId_    = 0;
    shapeCastTriangleIndex_ = -1;
    shapeCastX_ = shapeCastY_ = shapeCastZ_ = 0.f;
    shapeCastNormalX_ = shapeCastNormalY_ = shapeCastNormalZ_ = 0.f;
    shapeCastFraction_                                        = 0.f;
    shapeCastResults_.clear();
    constexpr int maxShapeCastResults = 4096;
    if (maxHits < 1 || maxHits > maxShapeCastResults)
        throw eve::Exception("World3D Shape Cast: maxHits must be in [1, 4096]");
    if (!isValid()) return 0;

    struct CastCollector {
        World3D *world    = nullptr;
        int      capacity = 0;

        static bool better(const ShapeCastResult &a, const ShapeCastResult &b) {
            return a.fraction < b.fraction || (a.fraction == b.fraction && a.shapeId < b.shapeId);
        }

        float clipFraction() const {
            if (static_cast<int>(world->shapeCastResults_.size()) < capacity) return 1.f;
            const auto worst =
                std::max_element(world->shapeCastResults_.begin(), world->shapeCastResults_.end(), better);
            return worst->fraction;
        }

        static float callback(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction, uint64_t materialId,
                              int triangleIndex, int, void *context) {
            auto *self = static_cast<CastCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return -1.f;
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
            auto           &results = self->world->shapeCastResults_;
            if (static_cast<int>(results.size()) < self->capacity) {
                results.push_back(candidate);
            } else {
                auto worst = std::max_element(results.begin(), results.end(), better);
                if (better(candidate, *worst)) *worst = candidate;
            }
            return self->clipFraction();
        }
    } collector{this, maxHits};
    shapeCastResults_.reserve(static_cast<size_t>(maxHits));
    b3QueryFilter filter = makeQueryFilter();
    b3World_CastShape(worldId_, origin, &proxy, translation, filter, &CastCollector::callback, &collector);
    std::sort(shapeCastResults_.begin(), shapeCastResults_.end(), CastCollector::better);
    if (!shapeCastResults_.empty()) {
        const ShapeCastResult &hit = shapeCastResults_.front();
        shapeCastBodyId_           = hit.bodyId;
        shapeCastShapeId_          = hit.shapeId;
        shapeCastShapeTag_         = hit.shapeTag;
        shapeCastMaterialId_       = hit.materialId;
        shapeCastTriangleIndex_    = hit.triangleIndex;
        shapeCastX_                = hit.x;
        shapeCastY_                = hit.y;
        shapeCastZ_                = hit.z;
        shapeCastNormalX_          = hit.normalX;
        shapeCastNormalY_          = hit.normalY;
        shapeCastNormalZ_          = hit.normalZ;
        shapeCastFraction_         = hit.fraction;
    }
    return static_cast<int>(shapeCastResults_.size());
}

int World3D::castSphere(float x, float y, float z, float radius, float dx, float dy, float dz) {
    castSphereAll(x, y, z, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !(radius >= 0.f) || !std::isfinite(radius) ||
        !std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
        throw eve::Exception("World3D.castSphere: all values must be finite; radius >= 0");
    const b3Vec3       point = b3Vec3_zero;
    const b3ShapeProxy proxy{&point, 1, radius};
    return castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::castCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx, float dy,
                         float dz) {
    castCapsuleAll(ax, ay, az, bx, by, bz, radius, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius, float dx,
                            float dy, float dz, int maxHits) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(bx) || !std::isfinite(by) ||
        !std::isfinite(bz) || !(radius >= 0.f) || !std::isfinite(radius) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.castCapsule: all values must be finite; radius >= 0");
    const b3Vec3       points[2] = {b3Vec3_zero, b3Vec3{bx - ax, by - ay, bz - az}};
    const b3ShapeProxy proxy{points, 2, radius};
    return castProxyAll(b3Pos{ax, ay, az}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::castBox(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                     float qw, float dx, float dy, float dz) {
    castBoxAll(x, y, z, width, height, depth, qx, qy, qz, qw, dx, dy, dz, 1);
    return shapeCastBodyId_;
}

int World3D::castBoxAll(float x, float y, float z, float width, float height, float depth, float qx, float qy, float qz,
                        float qw, float dx, float dy, float dz, int maxHits) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        throw eve::Exception("World3D.castBox: center and translation must be finite");
    const b3Quat rotation = normalizedQueryQuat(qx, qy, qz, qw, "World3D.castBox");
    b3Vec3       points[8];
    makeBoxProxyPoints(points, width, height, depth, rotation, "World3D.castBox");
    const b3ShapeProxy proxy{points, 8, 0.f};
    return castProxyAll(b3Pos{x, y, z}, proxy, b3Vec3{dx, dy, dz}, maxHits);
}

int World3D::closestPoint(float x, float y, float z, float maxDistance) {
    closestBodyId_   = -1;
    closestShapeId_  = -1;
    closestShapeTag_ = 0;
    closestX_ = closestY_ = closestZ_ = 0.f;
    closestNormalX_ = closestNormalY_ = closestNormalZ_ = 0.f;
    closestDistance_                                    = 0.f;
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
        World3D *world               = nullptr;
        b3Vec3   target              = b3Vec3_zero;
        float    bestDistanceSquared = 0.f;
        int      bodyId              = -1;
        int      stableShapeId       = -1;
        int      shapeTag            = 0;
        b3Vec3   point               = b3Vec3_zero;

        static bool callback(b3ShapeId shapeId, void *context) {
            auto *self = static_cast<ClosestCollector *>(context);
            if (self->world->shouldIgnoreQueryShape(shapeId)) return true;
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
    const b3QueryFilter filter = makeQueryFilter();
    b3World_OverlapAABB(worldId_, bounds, filter, &ClosestCollector::callback, &collector);
    if (collector.bodyId < 0) return -1;

    closestBodyId_   = collector.bodyId;
    closestShapeId_  = collector.stableShapeId;
    closestShapeTag_ = collector.shapeTag;
    closestX_        = collector.point.x;
    closestY_        = collector.point.y;
    closestZ_        = collector.point.z;
    closestDistance_ = std::sqrt(std::max(0.f, collector.bestDistanceSquared));
    if (closestDistance_ > 1e-6f) {
        const float inverseDistance = 1.f / closestDistance_;
        closestNormalX_             = (x - closestX_) * inverseDistance;
        closestNormalY_             = (y - closestY_) * inverseDistance;
        closestNormalZ_             = (z - closestZ_) * inverseDistance;
    }
    return closestBodyId_;
}

}  // namespace eve::physics
