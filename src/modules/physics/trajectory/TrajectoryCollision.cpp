#include "physics/trajectory/TrajectoryCollision.h"

#include "common/AttachmentPoint.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <utility>

namespace eve::physics::trajectory {
namespace {

AttachmentPoint addOffset(AttachmentPoint base, float dx, float dy, float dz) {
    return {base.x + dx, base.y + dy, base.z + dz};
}

bool finiteSample(const TrajectorySample& sample) {
    return sample.valid && std::isfinite(sample.start.x) && std::isfinite(sample.start.y) &&
           std::isfinite(sample.start.z) && std::isfinite(sample.end.x) && std::isfinite(sample.end.y) &&
           std::isfinite(sample.end.z);
}

class ScopedQueryFilter {
public:
    ScopedQueryFilter(World3D& world, QueryFilter3D filter)
        : world_(world), category_(world.getQueryCategoryBits()), mask_(world.getQueryMaskBits()),
          body_(world.getQueryIgnoredBodyId()), shape_(world.getQueryIgnoredShapeId()) {
        world_.setQueryFilter(static_cast<int>(filter.categoryBits), static_cast<int>(filter.maskBits));
        world_.setQueryIgnoredBodyId(filter.ignoredBodyId);
        world_.setQueryIgnoredShapeId(filter.ignoredShapeId);
    }
    ~ScopedQueryFilter() noexcept {
        world_.setQueryFilter(category_, mask_);
        world_.setQueryIgnoredBodyId(body_);
        world_.setQueryIgnoredShapeId(shape_);
    }

private:
    World3D& world_;
    int      category_;
    int      mask_;
    int      body_;
    int      shape_;
};

}  // namespace

TrajectoryCollisionRuntime::TrajectoryCollisionRuntime(World3D& world)
    : world_(&world), worldLifetime_(world.lifetimeToken()), worldHandle_(world.runtimeHandle()) {}

TrajectoryCollisionRuntime::~TrajectoryCollisionRuntime() = default;

World3D* TrajectoryCollisionRuntime::liveWorld() const noexcept {
    auto lifetime = worldLifetime_.lock();
    if (!lifetime || !world_ || !world_->isValid() || world_->runtimeHandle() != worldHandle_) return nullptr;
    return world_;
}

Result<void> TrajectoryCollisionRuntime::registerCollider(BoneColliderDefinition definition) {
    auto valid = definition.validate();
    if (!valid) return Result<void>::failure(valid.status());
    colliders_[definition.colliderId] = std::move(definition);
    return Result<void>::success();
}

Result<void> TrajectoryCollisionRuntime::unregisterCollider(std::string_view colliderId) {
    if (colliderId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider id is empty", "colliderId"));
    const std::string id(colliderId);
    const auto found = colliders_.find(id);
    if (found == colliders_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "bone collider catalog entry missing", id));

    for (auto it = armed_.begin(); it != armed_.end();) {
        if (it->second.colliderId == id)
            it = armed_.erase(it);
        else
            ++it;
    }
    for (auto it = hitMemory_.begin(); it != hitMemory_.end();) {
        if (std::get<1>(it->first) == id)
            it = hitMemory_.erase(it);
        else
            ++it;
    }
    colliders_.erase(found);
    return Result<void>::success();
}

Result<void> TrajectoryCollisionRuntime::bindPoseSource(SubjectRef subject, IAttachmentPointSource& source) {
    if (!subject.isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "trajectory subject is nil", "subject"));
    poses_[subject.format()] = &source;
    return Result<void>::success();
}

Result<void> TrajectoryCollisionRuntime::clearPoseSource(SubjectRef subject) {
    if (!subject.isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "trajectory subject is nil", "subject"));
    const std::string key = subject.format();
    poses_.erase(key);
    for (auto it = armed_.begin(); it != armed_.end();) {
        if (it->second.subject.format() == key)
            it = armed_.erase(it);
        else
            ++it;
    }
    for (auto it = hitMemory_.begin(); it != hitMemory_.end();) {
        if (std::get<0>(it->first) == key)
            it = hitMemory_.erase(it);
        else
            ++it;
    }
    return Result<void>::success();
}

Result<TrajectorySample> TrajectoryCollisionRuntime::sampleCollider(
    SubjectRef subject, const BoneColliderDefinition& def) const {
    const auto poseIt = poses_.find(subject.format());
    if (poseIt == poses_.end() || poseIt->second == nullptr)
        return Result<TrajectorySample>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "trajectory pose source is unbound", subject.format()));

    IAttachmentPointSource& source = *poseIt->second;
    TrajectorySample        sample;
    if (def.kind == BoneColliderShapeKind::Sphere) {
        auto point = source.sampleAttachmentPoint(def.boneName, def.localOffset);
        if (!point) return Result<TrajectorySample>::failure(point.status());
        sample.start = point.value();
        sample.end   = point.value();
        sample.valid = true;
        return Result<TrajectorySample>::success(sample);
    }

    if (!def.endBoneName.empty()) {
        auto start = source.sampleAttachmentPoint(def.boneName, def.localOffset);
        if (!start) return Result<TrajectorySample>::failure(start.status());
        auto end = source.sampleAttachmentPoint(def.endBoneName, def.endLocalOffset);
        if (!end) return Result<TrajectorySample>::failure(end.status());
        sample.start = start.value();
        sample.end   = end.value();
        sample.valid = true;
        return Result<TrajectorySample>::success(sample);
    }

    const AttachmentPoint aLocal =
        addOffset(def.localOffset, 0.f, -def.halfHeight, 0.f);
    const AttachmentPoint bLocal = addOffset(def.localOffset, 0.f, def.halfHeight, 0.f);
    auto start = source.sampleAttachmentPoint(def.boneName, aLocal);
    if (!start) return Result<TrajectorySample>::failure(start.status());
    auto end = source.sampleAttachmentPoint(def.boneName, bLocal);
    if (!end) return Result<TrajectorySample>::failure(end.status());
    sample.start = start.value();
    sample.end   = end.value();
    sample.valid = true;
    return Result<TrajectorySample>::success(sample);
}

Result<void> TrajectoryCollisionRuntime::arm(SubjectRef subject, std::string_view colliderId) {
    if (!subject.isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "trajectory subject is nil", "subject"));
    if (colliderId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider id is empty", "colliderId"));
    const std::string id(colliderId);
    const auto        catalog = colliders_.find(id);
    if (catalog == colliders_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "bone collider catalog entry missing", id));
    if (!liveWorld())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "trajectory world is no longer live", "world"));

    auto sample = sampleCollider(subject, catalog->second);
    if (!sample) return Result<void>::failure(sample.status());

    ArmedKey key{subject.format(), id};
    ArmedCollider armed;
    armed.subject    = subject;
    armed.colliderId = id;
    armed.previous   = sample.value();
    armed.current    = sample.value();
    armed.hasSample  = true;
    armed_[key]      = std::move(armed);
    return Result<void>::success();
}

Result<void> TrajectoryCollisionRuntime::disarm(SubjectRef subject, std::string_view colliderId) {
    if (!subject.isValid())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "trajectory subject is nil", "subject"));
    if (colliderId.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider id is empty", "colliderId"));
    const std::string id(colliderId);
    const ArmedKey    key{subject.format(), id};
    const auto        found = armed_.find(key);
    if (found == armed_.end())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "armed bone collider is missing", id));
    armed_.erase(found);
    for (auto it = hitMemory_.begin(); it != hitMemory_.end();) {
        if (std::get<0>(it->first) == subject.format() && std::get<1>(it->first) == id)
            it = hitMemory_.erase(it);
        else
            ++it;
    }
    return Result<void>::success();
}

Result<void> TrajectoryCollisionRuntime::sweepArmed(World3D& world, ArmedCollider& armed,
                                                    const BoneColliderDefinition& def,
                                                    std::vector<TrajectoryHit>& outHits) {
    if (!finiteSample(armed.previous) || !finiteSample(armed.current))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "trajectory sample is non-finite", "sample"));

    const float dx = armed.current.start.x - armed.previous.start.x;
    const float dy = armed.current.start.y - armed.previous.start.y;
    const float dz = armed.current.start.z - armed.previous.start.z;
    // Stationary frame: still overlap-test with a zero-length cast so resting contacts register once.
    QueryFilter3D filter;
    filter.categoryBits  = def.categoryBits;
    filter.maskBits      = def.maskBits;
    filter.ignoredBodyId = def.ignoredBodyId;

    ScopedQueryFilter scoped(world, filter);
    int hitCount = 0;
    if (def.kind == BoneColliderShapeKind::Sphere) {
        hitCount = world.castSphereAll(armed.previous.start.x, armed.previous.start.y, armed.previous.start.z,
                                       def.radius, dx, dy, dz, def.maxHits);
    } else {
        // Capsule endpoints move by the same translation as the start sample. Dual-socket blades
        // whose endpoints diverge within one frame are approximated by the start delta (UE5
        // single-sweep weapon traces use the same previous→current translation).
        hitCount =
            world.castCapsuleAll(armed.previous.start.x, armed.previous.start.y, armed.previous.start.z,
                                 armed.previous.end.x, armed.previous.end.y, armed.previous.end.z, def.radius, dx,
                                 dy, dz, def.maxHits);
    }

    for (int i = 0; i < hitCount; ++i) {
        const int bodyId  = world.getShapeCastResultBodyId(i);
        const int shapeId = world.getShapeCastResultShapeId(i);
        HitMemoryKey memory{armed.subject.format(), armed.colliderId, bodyId, shapeId};
        if (hitMemory_.contains(memory)) continue;
        hitMemory_[memory] = true;

        TrajectoryHit hit;
        hit.subject     = armed.subject;
        hit.colliderId  = armed.colliderId;
        hit.world       = worldHandle_;
        hit.bodyId      = bodyId;
        hit.shapeId     = shapeId;
        hit.shapeTag    = world.getShapeCastResultShapeTag(i);
        hit.materialId  = world.getShapeCastResultMaterialId(i);
        hit.x           = world.getShapeCastResultX(i);
        hit.y           = world.getShapeCastResultY(i);
        hit.z           = world.getShapeCastResultZ(i);
        hit.normalX     = world.getShapeCastResultNormalX(i);
        hit.normalY     = world.getShapeCastResultNormalY(i);
        hit.normalZ     = world.getShapeCastResultNormalZ(i);
        hit.fraction    = world.getShapeCastResultFraction(i);
        if (Body3D* body = world.findBodyById(bodyId)) hit.body = body->runtimeHandle();
        if (Shape3D* shape = world.findShapeById(shapeId)) hit.shape = shape->runtimeHandle();
        outHits.push_back(std::move(hit));
    }
    return Result<void>::success();
}

Result<TrajectoryFrame> TrajectoryCollisionRuntime::advance(SimulationTick tick) {
    if (tick.value() < lastTick_.value())
        return Result<TrajectoryFrame>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "trajectory tick must be non-decreasing", "tick"));
    World3D* world = liveWorld();
    if (!world)
        return Result<TrajectoryFrame>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "trajectory world is no longer live", "world"));

    TrajectoryFrame frame;
    frame.tick = tick;

    for (auto& [key, armed] : armed_) {
        (void)key;
        const auto catalog = colliders_.find(armed.colliderId);
        if (catalog == colliders_.end())
            return Result<TrajectoryFrame>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "armed bone collider catalog entry missing", armed.colliderId));

        auto sample = sampleCollider(armed.subject, catalog->second);
        if (!sample) return Result<TrajectoryFrame>::failure(sample.status());
        armed.previous  = armed.current;
        armed.current   = sample.value();
        armed.hasSample = true;

        auto swept = sweepArmed(*world, armed, catalog->second, frame.hits);
        if (!swept) return Result<TrajectoryFrame>::failure(swept.status());
    }

    lastTick_ = tick;
    return Result<TrajectoryFrame>::success(std::move(frame));
}

Result<TrajectorySample> TrajectoryCollisionRuntime::sampleArmed(SubjectRef subject,
                                                                 std::string_view colliderId) const {
    if (!subject.isValid())
        return Result<TrajectorySample>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "trajectory subject is nil", "subject"));
    if (colliderId.empty())
        return Result<TrajectorySample>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "bone collider id is empty", "colliderId"));
    const ArmedKey key{subject.format(), std::string(colliderId)};
    const auto     found = armed_.find(key);
    if (found == armed_.end() || !found->second.hasSample)
        return Result<TrajectorySample>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "armed bone collider sample missing",
                              std::string(colliderId)));
    return Result<TrajectorySample>::success(found->second.current);
}

}  // namespace eve::physics::trajectory
