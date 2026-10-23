#include "physics/destruction/GeometryCollectionInstance.h"

#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "physics/destruction/FractureGeometry.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

namespace eve::physics {
namespace {

constexpr float kSleepSpeed = 0.05f;

void setBoneBodyType(Body3D& body, BoneRuntimeState state) {
    switch (state) {
        case BoneRuntimeState::Attached:
        case BoneRuntimeState::Sleeping:
            body.setType("static");
            break;
        case BoneRuntimeState::Detached:
            body.setType("dynamic");
            body.setAwake(true);
            break;
    }
}

}  // namespace

GeometryCollectionInstance::GeometryCollectionInstance(PhysicsWorldHandle worldHandle, float originX, float originY,
                                                       float originZ)
    : worldHandle_(worldHandle), originX_(originX), originY_(originY), originZ_(originZ) {}

GeometryCollectionInstance::~GeometryCollectionInstance() { releaseBodies(); }

eve::Result<std::unique_ptr<GeometryCollectionInstance>> GeometryCollectionInstance::create(
    World3D& world, const GeometryCollectionAsset& asset, float originX, float originY, float originZ) {
    return createInternal(world, asset, {}, originX, originY, originZ);
}
std::shared_ptr<const FractureGeometry> GeometryCollectionInstance::fragmentGeometry() const noexcept {
    return geometry_;
}
eve::Result<std::unique_ptr<GeometryCollectionInstance>> GeometryCollectionInstance::createFractured(
    World3D& world, const GeometryCollectionAsset& asset, const FractureGeometry& geometry, float x, float y, float z) {
    auto valid = geometry.validate();
    if (!valid) return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(valid.status());
    if (geometry.fragments.size() != asset.bones.size())
        return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fragment count does not match bones", "geometry"));
    for (std::size_t i = 0; i < geometry.fragments.size(); ++i) {
        const auto& b = asset.bones[i];
        const auto& f = geometry.fragments[i];
        for (std::size_t v = 0; v < f.positions.size(); v += 3)
            if (std::abs(f.positions[v]) > b.halfExtentX + 1e-5f ||
                std::abs(f.positions[v + 1]) > b.halfExtentY + 1e-5f ||
                std::abs(f.positions[v + 2]) > b.halfExtentZ + 1e-5f)
                return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "fragment vertices exceed declared bounds", "geometry"));
    }
    return createInternal(world, asset, std::make_shared<const FractureGeometry>(geometry), x, y, z);
}
eve::Result<std::unique_ptr<GeometryCollectionInstance>> GeometryCollectionInstance::createInternal(
    World3D& world, const GeometryCollectionAsset& asset, std::shared_ptr<const FractureGeometry> geometry,
    float originX, float originY, float originZ) {
    auto valid = asset.validate();
    if (!valid) return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(valid.status());
    if (!std::isfinite(originX) || !std::isfinite(originY) || !std::isfinite(originZ))
        return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "instance origin must be finite", "origin"));
    if (!world.isValid())
        return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection create requires a live World3D", "world"));

    auto instance = std::unique_ptr<GeometryCollectionInstance>(
        new GeometryCollectionInstance(world.runtimeHandle(), originX, originY, originZ));
    instance->geometry_ = std::move(geometry);
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
        // Intact Attached bones stay static so connection-graph topology (not
        // independent dynamic bodies) keeps the collection from falling apart
        // before any edge breaks. Detach sync promotes free islands to dynamic.
        Body3D* body = world.newBody("static", x, y, z);
        if (!body || !body->isValid()) {
            rollback();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "geometry-collection failed to create a bone body", "body"));
        }
        try {
            if (instance->geometry_) {
                const auto& fragment = instance->geometry_->fragments[instance->bones_.size()];
                body->newConvexHullShape(fragment.positions, 254, boneDef.density, boneDef.friction,
                                         boneDef.restitution);
            } else {
                body->newBoxShape(boneDef.halfExtentX * 2.f, boneDef.halfExtentY * 2.f, boneDef.halfExtentZ * 2.f,
                                  boneDef.density, boneDef.friction, boneDef.restitution);
            }
        } catch (const std::exception& error) {
            body->destroy();
            rollback();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "fragmentCollider"));
        }
        auto link = PhysicsLink::fromBody(*body);
        if (!link) {
            rollback();
            if (body->isValid()) body->destroy();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(link.status());
        }
        BoneRuntime bone;
        bone.state         = BoneRuntimeState::Attached;
        bone.anchored     = boneDef.anchoredDefault;
        bone.link         = link.value();
        bone.halfExtentX  = boneDef.halfExtentX;
        bone.halfExtentY  = boneDef.halfExtentY;
        bone.halfExtentZ  = boneDef.halfExtentZ;
        bone.clusterId    = boneDef.clusterId;
        bone.fractureLevel = boneDef.fractureLevel;
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

    if (instance->geometry_) {
        auto constraints = instance->stageConstraints(instance->edges_);
        if (!constraints) {
            rollback();
            return eve::Result<std::unique_ptr<GeometryCollectionInstance>>::failure(constraints.status());
        }
        instance->ownedJoints_ = std::move(constraints).takeValue();
        for (std::size_t i = 0; i < instance->edges_.size(); ++i)
            instance->edges_[i].joint =
                instance->ownedJoints_[i] ? instance->ownedJoints_[i]->runtimeHandle() : PhysicsJointHandle::invalid();
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
    if (field.kind == DestructionFieldKind::Impulse &&
        (!std::isfinite(field.dirX) || !std::isfinite(field.dirY) || !std::isfinite(field.dirZ)))
        return eve::Result<FieldApplicationReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "impulse field direction must be finite", "field"));
    World3D* world = resolveWorld();
    if (!world) {
        orphanedPhysics_ = true;
        return eve::Result<FieldApplicationReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    }

    FieldApplicationReceipt receipt;
    int sleepsThisCall = 0;
    bool needGraphSync = false;
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        auto bodyResult = bones_[i].link.resolve(*world);
        if (!bodyResult) continue;
        Body3D* body = bodyResult.value();
        const float weight = falloffAt(field, body->getX(), body->getY(), body->getZ());
        if (weight <= 0.f) continue;
        switch (field.kind) {
            case DestructionFieldKind::Anchor: {
                const bool wasSleeping = bones_[i].state == BoneRuntimeState::Sleeping;
                bones_[i].anchored = true;
                bones_[i].state    = BoneRuntimeState::Attached;
                body->setType("static");
                if (wasSleeping) ++sleepBatchRevision_;
                ++receipt.bonesAffected;
                needGraphSync = true;
                break;
            }
            case DestructionFieldKind::Impulse: {
                // Anchored foundations and still-Attached graph members stay put.
                if (bones_[i].anchored || bones_[i].state == BoneRuntimeState::Attached) break;
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
                const bool wasSleeping = bones_[i].state == BoneRuntimeState::Sleeping;
                body->setType("dynamic");
                body->applyLinearImpulse(dx * impulse, dy * impulse, dz * impulse);
                bones_[i].state = BoneRuntimeState::Detached;
                if (wasSleeping) ++sleepBatchRevision_;
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
                if (budget_.maxSleepsPerStep > 0 && sleepsThisCall >= budget_.maxSleepsPerStep) {
                    ++receipt.sleepsDeferred;
                    break;
                }
                body->setType("static");
                if (bones_[i].state != BoneRuntimeState::Sleeping) ++sleepBatchRevision_;
                bones_[i].state = BoneRuntimeState::Sleeping;
                ++receipt.bonesAffected;
                ++sleepsThisCall;
                break;
            }
            case DestructionFieldKind::Strain:
                break;
        }
    }
    if (needGraphSync) syncBoneActivationFromGraph();

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

void GeometryCollectionInstance::breakEdge(int edgeIndex, std::uint64_t tick, DestructionStepReceipt& receipt) {
    auto& edge = edges_[static_cast<std::size_t>(edgeIndex)];
    if (edge.broken) return;
    edge.broken = true;
    if (static_cast<std::size_t>(edgeIndex) < ownedJoints_.size())
        ownedJoints_[static_cast<std::size_t>(edgeIndex)].reset();
    edge.joint = PhysicsJointHandle::invalid();
    ++receipt.edgesBroken;

    BoneDetachEvent event;
    event.boneA = edge.boneA;
    event.boneB = edge.boneB;
    event.tick  = static_cast<std::int64_t>(tick);
    detachEvents_.push_back(event);

    if (edge.boneA >= 0 && edge.boneB >= 0 && edge.boneA < static_cast<int>(bones_.size()) &&
        edge.boneB < static_cast<int>(bones_.size())) {
        const int clusterA = bones_[static_cast<std::size_t>(edge.boneA)].clusterId;
        const int clusterB = bones_[static_cast<std::size_t>(edge.boneB)].clusterId;
        if (clusterA != clusterB) {
            ClusterBreakEvent clusterEvent;
            clusterEvent.clusterA = clusterA;
            clusterEvent.clusterB = clusterB;
            clusterEvent.boneA    = edge.boneA;
            clusterEvent.boneB    = edge.boneB;
            clusterEvent.tick     = static_cast<std::int64_t>(tick);
            clusterBreakEvents_.push_back(clusterEvent);
            ++receipt.clusterBreaks;
        }
    }
}

void GeometryCollectionInstance::syncBoneActivationFromGraph() {
    const int boneCount = static_cast<int>(bones_.size());
    if (boneCount == 0) return;

    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(boneCount));
    for (const auto& edge : edges_) {
        if (edge.broken) continue;
        if (edge.boneA < 0 || edge.boneB < 0 || edge.boneA >= boneCount || edge.boneB >= boneCount) continue;
        adjacency[static_cast<std::size_t>(edge.boneA)].push_back(edge.boneB);
        adjacency[static_cast<std::size_t>(edge.boneB)].push_back(edge.boneA);
    }

    std::vector<int> component(static_cast<std::size_t>(boneCount), -1);
    int componentCount = 0;
    bool anyAnchored = false;
    for (int i = 0; i < boneCount; ++i) {
        if (bones_[static_cast<std::size_t>(i)].anchored) anyAnchored = true;
        if (component[static_cast<std::size_t>(i)] >= 0) continue;
        std::queue<int> queue;
        queue.push(i);
        component[static_cast<std::size_t>(i)] = componentCount;
        while (!queue.empty()) {
            const int cur = queue.front();
            queue.pop();
            for (int next : adjacency[static_cast<std::size_t>(cur)]) {
                if (component[static_cast<std::size_t>(next)] >= 0) continue;
                component[static_cast<std::size_t>(next)] = componentCount;
                queue.push(next);
            }
        }
        ++componentCount;
    }

    std::vector<bool> componentHasAnchor(static_cast<std::size_t>(componentCount), false);
    for (int i = 0; i < boneCount; ++i) {
        if (bones_[static_cast<std::size_t>(i)].anchored)
            componentHasAnchor[static_cast<std::size_t>(component[static_cast<std::size_t>(i)])] = true;
    }

    World3D* world = resolveWorld();
    for (int i = 0; i < boneCount; ++i) {
        auto& bone = bones_[static_cast<std::size_t>(i)];
        if (bone.state == BoneRuntimeState::Sleeping) continue; // Sleep is sticky until Impulse/Anchor
        const bool stayAttached =
            bone.anchored ||
            componentHasAnchor[static_cast<std::size_t>(component[static_cast<std::size_t>(i)])] ||
            (!anyAnchored && componentCount == 1);
        const BoneRuntimeState desired =
            stayAttached ? BoneRuntimeState::Attached : BoneRuntimeState::Detached;
        if (bone.state == desired) {
            // Ensure Attached bones remain static even if a prior path made them dynamic.
            if (desired == BoneRuntimeState::Attached && world) {
                auto body = bone.link.resolve(*world);
                if (body) setBoneBodyType(*body.value(), desired);
            }
            continue;
        }
        bone.state = desired;
        if (!world) continue;
        auto body = bone.link.resolve(*world);
        if (body) setBoneBodyType(*body.value(), desired);
    }
}

eve::Result<DestructionStepReceipt> GeometryCollectionInstance::step(SimulationStep step) {
    DestructionStepReceipt receipt;
    if (step.delta.nanoseconds() < 0)
        return eve::Result<DestructionStepReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection step delta must be non-negative", "step"));
    const std::uint64_t tick = step.tick.value();
    if (lastTick_ != 0 && tick <= lastTick_)
        return eve::Result<DestructionStepReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection step tick must advance", "step"));
    World3D* world = resolveWorld();
    if (!world) {
        orphanedPhysics_ = true;
        return eve::Result<DestructionStepReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    }
    orphanedPhysics_ = false;
    detachEvents_.clear();
    clusterBreakEvents_.clear();

    std::vector<int> toBreak = pendingEdgeBreaks_;
    pendingEdgeBreaks_.clear();
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        const auto& edge = edges_[i];
        if (edge.broken) continue;
        if (edge.accumulatedStrain >= edge.strainThreshold) toBreak.push_back(static_cast<int>(i));
    }
    std::sort(toBreak.begin(), toBreak.end());
    toBreak.erase(std::unique(toBreak.begin(), toBreak.end()), toBreak.end());

    const int budget =
        budget_.maxEdgeBreaksPerStep > 0 ? budget_.maxEdgeBreaksPerStep : static_cast<int>(toBreak.size());
    int brokenThisStep = 0;
    for (int edgeIndex : toBreak) {
        if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges_.size())) continue;
        if (edges_[static_cast<std::size_t>(edgeIndex)].broken) continue;
        if (brokenThisStep >= budget) {
            pendingEdgeBreaks_.push_back(edgeIndex);
            ++receipt.edgesDeferred;
            continue;
        }
        breakEdge(edgeIndex, tick, receipt);
        ++brokenThisStep;
    }
    if (receipt.edgesBroken > 0) syncBoneActivationFromGraph();
    lastTick_ = tick;
    return eve::Result<DestructionStepReceipt>::success(receipt);
}

BoneRuntimeState GeometryCollectionInstance::boneState(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) return BoneRuntimeState::Attached;
    return bones_[static_cast<std::size_t>(boneIndex)].state;
}

int GeometryCollectionInstance::boneClusterId(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) return -1;
    return bones_[static_cast<std::size_t>(boneIndex)].clusterId;
}

float GeometryCollectionInstance::edgeStrain(int edgeIndex) const {
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges_.size())) return 0.f;
    return edges_[static_cast<std::size_t>(edgeIndex)].accumulatedStrain;
}

bool GeometryCollectionInstance::isEdgeBroken(int edgeIndex) const {
    if (edgeIndex < 0 || edgeIndex >= static_cast<int>(edges_.size())) return false;
    return edges_[static_cast<std::size_t>(edgeIndex)].broken;
}

BoneDetachEvent GeometryCollectionInstance::detachEventAt(int index) const {
    if (index < 0 || index >= static_cast<int>(detachEvents_.size())) return {};
    return detachEvents_[static_cast<std::size_t>(index)];
}

ClusterBreakEvent GeometryCollectionInstance::clusterBreakEventAt(int index) const {
    if (index < 0 || index >= static_cast<int>(clusterBreakEvents_.size())) return {};
    return clusterBreakEvents_[static_cast<std::size_t>(index)];
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

eve::Result<BonePresentation> GeometryCollectionInstance::bonePresentation(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size()))
        return eve::Result<BonePresentation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "geometry-collection bone index is out of range", "bone"));
    World3D* world = resolveWorld();
    if (!world)
        return eve::Result<BonePresentation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    const auto& bone = bones_[static_cast<std::size_t>(boneIndex)];
    auto body = bone.link.resolve(*world);
    if (!body) return eve::Result<BonePresentation>::failure(body.status());
    BonePresentation presentation;
    presentation.state         = bone.state;
    presentation.halfExtentX  = bone.halfExtentX;
    presentation.halfExtentY  = bone.halfExtentY;
    presentation.halfExtentZ  = bone.halfExtentZ;
    presentation.worldX       = body.value()->getX();
    presentation.worldY       = body.value()->getY();
    presentation.worldZ       = body.value()->getZ();
    presentation.rotX         = body.value()->getRotX();
    presentation.rotY         = body.value()->getRotY();
    presentation.rotZ         = body.value()->getRotZ();
    presentation.rotW         = body.value()->getRotW();
    presentation.clusterId    = bone.clusterId;
    presentation.fractureLevel = bone.fractureLevel;
    return eve::Result<BonePresentation>::success(presentation);
}

eve::Result<GeometryCollectionInstanceSnapshot> GeometryCollectionInstance::captureSnapshot() const {
    GeometryCollectionInstanceSnapshot snapshot;
    snapshot.originX            = originX_;
    snapshot.originY            = originY_;
    snapshot.originZ            = originZ_;
    snapshot.lastTick           = static_cast<std::int64_t>(lastTick_);
    snapshot.sleepBatchRevision = sleepBatchRevision_;
    snapshot.bones.reserve(bones_.size());
    for (const auto& bone : bones_) {
        GeometryCollectionInstanceSnapshot::BoneSnapshot boneSnap;
        boneSnap.state         = static_cast<std::uint8_t>(bone.state);
        boneSnap.anchored     = bone.anchored;
        boneSnap.clusterId    = bone.clusterId;
        boneSnap.fractureLevel = bone.fractureLevel;
        boneSnap.halfExtentX  = bone.halfExtentX;
        boneSnap.halfExtentY  = bone.halfExtentY;
        boneSnap.halfExtentZ  = bone.halfExtentZ;
        snapshot.bones.push_back(boneSnap);
    }
    snapshot.edges.reserve(edges_.size());
    for (const auto& edge : edges_) {
        GeometryCollectionInstanceSnapshot::EdgeSnapshot edgeSnap;
        edgeSnap.boneA             = edge.boneA;
        edgeSnap.boneB             = edge.boneB;
        edgeSnap.strainThreshold   = edge.strainThreshold;
        edgeSnap.accumulatedStrain = edge.accumulatedStrain;
        edgeSnap.broken            = edge.broken;
        snapshot.edges.push_back(edgeSnap);
    }
    auto valid = snapshot.validate();
    if (!valid) return eve::Result<GeometryCollectionInstanceSnapshot>::failure(valid.status());
    return eve::Result<GeometryCollectionInstanceSnapshot>::success(std::move(snapshot));
}

eve::Result<void> GeometryCollectionInstance::restoreSnapshot(const GeometryCollectionInstanceSnapshot& snapshot) {
    auto valid = snapshot.validate();
    if (!valid) return eve::Result<void>::failure(valid.status());
    if (snapshot.bones.size() != bones_.size() || snapshot.edges.size() != edges_.size())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "instance-snapshot bone/edge counts must match the live instance", "snapshot"));
    World3D* world = resolveWorld();
    if (!world) {
        orphanedPhysics_ = true;
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "geometry-collection world is gone", "world"));
    }

    // Stage + resolve every body before mutating World3D so failures leave both
    // the instance topology and body types unchanged.
    std::vector<BoneRuntime> stagedBones = bones_;
    std::vector<EdgeRuntime> stagedEdges = edges_;
    std::vector<Body3D*>     bodies(snapshot.bones.size(), nullptr);
    for (std::size_t i = 0; i < snapshot.bones.size(); ++i) {
        const auto& src = snapshot.bones[i];
        if (geometry_ && (src.halfExtentX != bones_[i].halfExtentX || src.halfExtentY != bones_[i].halfExtentY ||
                          src.halfExtentZ != bones_[i].halfExtentZ || src.clusterId != bones_[i].clusterId ||
                          src.fractureLevel != bones_[i].fractureLevel))
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "snapshot cannot replace immutable fragment metadata", "snapshot"));
        stagedBones[i].state         = static_cast<BoneRuntimeState>(src.state);
        stagedBones[i].anchored      = src.anchored;
        stagedBones[i].clusterId     = src.clusterId;
        stagedBones[i].fractureLevel = src.fractureLevel;
        stagedBones[i].halfExtentX   = src.halfExtentX;
        stagedBones[i].halfExtentY   = src.halfExtentY;
        stagedBones[i].halfExtentZ   = src.halfExtentZ;
        auto body = stagedBones[i].link.resolve(*world);
        if (!body) return eve::Result<void>::failure(body.status());
        bodies[i] = body.value();
    }
    for (std::size_t i = 0; i < snapshot.edges.size(); ++i) {
        const auto& src = snapshot.edges[i];
        if (src.boneA != stagedEdges[i].boneA || src.boneB != stagedEdges[i].boneB)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "instance-snapshot edge topology mismatch", "edges"));
        stagedEdges[i].strainThreshold   = src.strainThreshold;
        stagedEdges[i].accumulatedStrain = src.accumulatedStrain;
        stagedEdges[i].broken            = src.broken;
    }

    if (geometry_) {
        auto stagedJoints = stageConstraints(stagedEdges);
        if (!stagedJoints) return eve::Result<void>::failure(stagedJoints.status());
        releaseConstraints();
        ownedJoints_ = std::move(stagedJoints).takeValue();
        for (std::size_t i = 0; i < stagedEdges.size(); ++i)
            stagedEdges[i].joint = ownedJoints_[i] ? ownedJoints_[i]->runtimeHandle() : PhysicsJointHandle::invalid();
    }
    for (std::size_t i = 0; i < stagedBones.size(); ++i) {
        Body3D* body = bodies[i];
        switch (stagedBones[i].state) {
            case BoneRuntimeState::Attached:
                body->setType(stagedBones[i].anchored ? "static" : "dynamic");
                break;
            case BoneRuntimeState::Detached:
                body->setType("dynamic");
                body->setAwake(true);
                break;
            case BoneRuntimeState::Sleeping:
                body->setType("static");
                break;
        }
    }

    bones_              = std::move(stagedBones);
    edges_              = std::move(stagedEdges);
    originX_            = snapshot.originX;
    originY_            = snapshot.originY;
    originZ_            = snapshot.originZ;
    lastTick_           = static_cast<std::uint64_t>(snapshot.lastTick);
    sleepBatchRevision_ = snapshot.sleepBatchRevision;
    detachEvents_.clear();
    clusterBreakEvents_.clear();
    pendingEdgeBreaks_.clear();
    orphanedPhysics_ = false;
    return eve::Result<void>::success();
}

void GeometryCollectionInstance::destroyOwnedBodies(World3D& world) {
    for (auto& bone : bones_) {
        auto body = bone.link.resolve(world);
        if (body && body.value() && body.value()->isValid()) body.value()->destroy();
        bone.link = {};
    }
}

void GeometryCollectionInstance::releaseBodies() {
    releaseConstraints();
    if (World3D* world = resolveWorld()) {
        destroyOwnedBodies(*world);
    } else {
        for (auto& bone : bones_) bone.link = {};
        orphanedPhysics_ = true;
    }
}

}  // namespace eve::physics
