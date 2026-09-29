#include "physics/destruction/GeometryCollectionInstance.h"

#include "physics/Body3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <utility>

namespace eve::physics {
namespace {

constexpr float kSleepSpeed = 0.05f;

}  // namespace

GeometryCollectionInstance::GeometryCollectionInstance(PhysicsWorldHandle worldHandle, float originX, float originY,
                                                       float originZ)
    : worldHandle_(worldHandle), originX_(originX), originY_(originY), originZ_(originZ) {}

GeometryCollectionInstance::~GeometryCollectionInstance() { releaseBodies(); }

eve::Result<std::unique_ptr<GeometryCollectionInstance>> GeometryCollectionInstance::create(
    World3D& world, const GeometryCollectionAsset& asset, float originX, float originY, float originZ) {
    auto valid = asset.validate();
    if (!valid) return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(valid.status());
    if (!world.isValid())
        return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection create requires a live World3D", "world"));

    auto instance = std::unique_ptr<GeometryCollectionInstance>(
        new GeometryCollectionInstance(world.runtimeHandle(), originX, originY, originZ));
    instance->bones_.reserve(asset.bones.size());
    instance->edges_.reserve(asset.edges.size());

    std::vector<Body3D*> createdBodies;
    createdBodies.reserve(asset.bones.size());
    auto rollback = [&]() {
        for (Body3D* body : createdBodies) {
            if (body && body->isValid()) body->destroy();
        }
        createdBodies.clear();
        instance->bones_.clear();
    };

    for (const auto& boneDef : asset.bones) {
        const float x = originX + boneDef.localX;
        const float y = originY + boneDef.localY;
        const float z = originZ + boneDef.localZ;
        const char* type = boneDef.anchoredDefault ? "static" : "dynamic";
        Body3D* body = world.newBody(type, x, y, z);
        if (!body || !body->isValid()) {
            rollback();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "geometry-collection failed to create a bone body", "body"));
        }
        body->newBoxShape(boneDef.halfExtentX * 2.f, boneDef.halfExtentY * 2.f, boneDef.halfExtentZ * 2.f,
                          boneDef.density, boneDef.friction, boneDef.restitution);
        auto link = PhysicsLink::fromBody(*body);
        if (!link) {
            rollback();
            if (body->isValid()) body->destroy();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(link.status());
        }
        BoneRuntime bone;
        bone.state       = BoneRuntimeState::Attached;
        bone.anchored   = boneDef.anchoredDefault;
        bone.link       = link.value();
        bone.halfExtentX = boneDef.halfExtentX;
        bone.halfExtentY = boneDef.halfExtentY;
        bone.halfExtentZ = boneDef.halfExtentZ;
        instance->bones_.push_back(bone);
        createdBodies.push_back(body);
    }

    for (const auto& edgeDef : asset.edges) {
        EdgeRuntime edge;
        edge.boneA           = edgeDef.boneA;
        edge.boneB           = edgeDef.boneB;
        edge.strainThreshold = edgeDef.strainThreshold;
        instance->edges_.push_back(edge);
    }

    return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::success(std::move(instance));
}

bool GeometryCollectionInstance::hasLiveWorld() const noexcept { return resolveWorld() != nullptr; }

World3D* GeometryCollectionInstance::resolveWorld() const noexcept {
    World3D* world = World3D::findWorld(worldHandle_);
    if (!world || !world->isValid()) return nullptr;
    return world;
}

eve::Result<Body3D*> GeometryCollectionInstance::resolveBoneBody(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size()))
        return eve::Result<Body3D*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone index is out of range", "bone"));
    World3D* world = resolveWorld();
    if (!world)
        return eve::Result<Body3D*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    return bones_[static_cast<std::size_t>(boneIndex)].link.resolve(*world);
}

float GeometryCollectionInstance::falloffAt(const DestructionField& field, float x, float y, float z) const {
    const float dx = x - field.centerX;
    const float dy = y - field.centerY;
    const float dz = z - field.centerZ;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(field.radius > 0.f) || !std::isfinite(field.radius)) return 0.f;
    if (dist > field.radius) return 0.f;
    if (field.falloff == DestructionFieldFalloff::None) return 1.f;
    return 1.f - dist / field.radius;
}

eve::Result<FieldApplicationReceipt> GeometryCollectionInstance::applyField(const DestructionField& field) {
    if (!std::isfinite(field.centerX) || !std::isfinite(field.centerY) || !std::isfinite(field.centerZ) ||
        !std::isfinite(field.radius) || field.radius < 0.f || !std::isfinite(field.magnitude))
        return eve::Result<FieldApplicationReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "destruction field parameters must be finite", "field"));
    World3D* world = resolveWorld();
    if (!world) {
        orphanedPhysics_ = true;
        return eve::Result<FieldApplicationReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    }

    FieldApplicationReceipt receipt;
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        auto bodyResult = bones_[i].link.resolve(*world);
        if (!bodyResult) continue;
        Body3D* body = bodyResult.value();
        const float weight = falloffAt(field, body->getX(), body->getY(), body->getZ());
        if (weight <= 0.f) continue;
        switch (field.kind) {
            case DestructionFieldKind::Anchor:
                bones_[i].anchored = true;
                bones_[i].state    = BoneRuntimeState::Attached;
                body->setType("static");
                ++receipt.bonesAffected;
                break;
            case DestructionFieldKind::Impulse: {
                if (bones_[i].state == BoneRuntimeState::Attached && !bones_[i].anchored) break;
                float dx = field.dirX, dy = field.dirY, dz = field.dirZ;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (len <= 1e-6f) {
                    dx = 0.f;
                    dy = 1.f;
                    dz = 0.f;
                } else {
                    dx /= len;
                    dy /= len;
                    dz /= len;
                }
                const float impulse = field.magnitude * weight;
                body->setType("dynamic");
                body->applyLinearImpulse(dx * impulse, dy * impulse, dz * impulse);
                bones_[i].state = BoneRuntimeState::Detached;
                ++receipt.bonesAffected;
                break;
            }
            case DestructionFieldKind::Sleep: {
                if (bones_[i].state != BoneRuntimeState::Detached) break;
                const float vx = body->getLinearVelocityX();
                const float vy = body->getLinearVelocityY();
                const float vz = body->getLinearVelocityZ();
                const float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
                if (speed > kSleepSpeed) break;
                body->setType("static");
                bones_[i].state = BoneRuntimeState::Sleeping;
                ++receipt.bonesAffected;
                break;
            }
            case DestructionFieldKind::Strain:
                break;
        }
    }

    if (field.kind == DestructionFieldKind::Strain) {
        for (auto& edge : edges_) {
            if (edge.broken) continue;
            if (edge.boneA < 0 || edge.boneB < 0 || edge.boneA >= static_cast<int>(bones_.size()) ||
                edge.boneB >= static_cast<int>(bones_.size()))
                continue;
            auto bodyA = bones_[static_cast<std::size_t>(edge.boneA)].link.resolve(*world);
            auto bodyB = bones_[static_cast<std::size_t>(edge.boneB)].link.resolve(*world);
            if (!bodyA || !bodyB) continue;
            const float midX = 0.5f * (bodyA.value()->getX() + bodyB.value()->getX());
            const float midY = 0.5f * (bodyA.value()->getY() + bodyB.value()->getY());
            const float midZ = 0.5f * (bodyA.value()->getZ() + bodyB.value()->getZ());
            const float weight = falloffAt(field, midX, midY, midZ);
            if (weight <= 0.f) continue;
            edge.accumulatedStrain += field.magnitude * weight;
            ++receipt.edgesAffected;
        }
    }
    return eve::Result<FieldApplicationReceipt>::success(receipt);
}

void GeometryCollectionInstance::breakEdge(int edgeIndex, std::uint64_t tick) {
    auto& edge = edges_[static_cast<std::size_t>(edgeIndex)];
    if (edge.broken) return;
    edge.broken = true;
    World3D* world = resolveWorld();
    for (int boneIndex : {edge.boneA, edge.boneB}) {
        if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) continue;
        auto& bone = bones_[static_cast<std::size_t>(boneIndex)];
        if (bone.anchored) continue;
        bone.state = BoneRuntimeState::Detached;
        if (!world) continue;
        auto body = bone.link.resolve(*world);
        if (body) {
            body.value()->setType("dynamic");
            body.value()->setAwake(true);
        }
    }
    BoneDetachEvent event;
    event.boneA = edge.boneA;
    event.boneB = edge.boneB;
    event.tick  = static_cast<std::int64_t>(tick);
    detachEvents_.push_back(event);
}

eve::Result<void> GeometryCollectionInstance::step(SimulationStep step) {
    if (step.delta.nanoseconds() < 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection step delta must be non-negative", "step"));
    const std::uint64_t tick = step.tick.value();
    if (lastTick_ != 0 && tick <= lastTick_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection step tick must advance", "step"));
    World3D* world = resolveWorld();
    if (!world) {
        orphanedPhysics_ = true;
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    }
    orphanedPhysics_ = false;
    detachEvents_.clear();

    std::vector<int> toBreak;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        const auto& edge = edges_[i];
        if (edge.broken) continue;
        if (edge.accumulatedStrain >= edge.strainThreshold) toBreak.push_back(static_cast<int>(i));
    }
    for (int edgeIndex : toBreak) breakEdge(edgeIndex, tick);
    lastTick_ = tick;
    return eve::Result<void>::success();
}

BoneRuntimeState GeometryCollectionInstance::boneState(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) return BoneRuntimeState::Attached;
    return bones_[static_cast<std::size_t>(boneIndex)].state;
}

float GeometryCollectionInstance::edgeStrain(int edgeIndex) const {
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges_.size())) return 0.f;
    return edges_[static_cast<std::size_t>(edgeIndex)].accumulatedStrain;
}

bool GeometryCollectionInstance::edgeBroken(int edgeIndex) const {
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges_.size())) return false;
    return edges_[static_cast<std::size_t>(edgeIndex)].broken;
}

BoneDetachEvent GeometryCollectionInstance::detachEventAt(int index) const {
    if (index < 0 || index >= static_cast<int>(detachEvents_.size())) return {};
    return detachEvents_[static_cast<std::size_t>(index)];
}

eve::Result<PhysicsLink> GeometryCollectionInstance::boneLink(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size()))
        return eve::Result<PhysicsLink>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone index is out of range", "bone"));
    World3D* world = resolveWorld();
    if (!world)
        return eve::Result<PhysicsLink>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    auto body = bones_[static_cast<std::size_t>(boneIndex)].link.resolve(*world);
    if (!body) return eve::Result<PhysicsLink>::failure(body.status());
    return eve::Result<PhysicsLink>::success(bones_[static_cast<std::size_t>(boneIndex)].link);
}

void GeometryCollectionInstance::destroyOwnedBodies(World3D& world) {
    for (auto& bone : bones_) {
        auto body = bone.link.resolve(world);
        if (body && body.value() && body.value()->isValid()) body.value()->destroy();
        bone.link = {};
    }
}

void GeometryCollectionInstance::releaseBodies() {
    if (World3D* world = resolveWorld()) {
        destroyOwnedBodies(*world);
    } else {
        for (auto& bone : bones_) bone.link = {};
        orphanedPhysics_ = true;
    }
}

}  // namespace eve::physics
