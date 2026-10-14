#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/PhysicsCapabilities.h"
#include "physics/Shape3D.h"
#include "physics/box3d/Box3DWorldFactory.h"
#if EVENGINE_HAS_JOLT
#include "physics/jolt/JoltWorldFactory.h"
#endif

#include "common/Exception.h"
#include "common/Profile.h"
#include "platform_event/PlatformEvent.h"


#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "physics/World3DInternal.h"

namespace eve::physics {
using namespace world3d_detail;
namespace {

uint64_t stablePairKey(int idA, int idB) {
    const uint32_t low = static_cast<uint32_t>(std::min(idA, idB));
    const uint32_t high = static_cast<uint32_t>(std::max(idA, idB));
    return (static_cast<uint64_t>(high) << 32) | low;
}

bool pairContains(uint64_t key, int id) {
    const uint32_t value = static_cast<uint32_t>(id);
    return static_cast<uint32_t>(key) == value || static_cast<uint32_t>(key >> 32) == value;
}

eve::Result<eve::SimulationStep> makeLegacyStep(float dt, eve::SimulationTick currentTick) {
    if (!std::isfinite(dt) || dt < 0.f) {
        return eve::Result<eve::SimulationStep>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "World3D update dt must be finite and non-negative", "physics.world3d.update.dt"));
    }
    const auto nextTick = currentTick.incremented();
    if (!nextTick) {
        return eve::Result<eve::SimulationStep>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "World3D simulation tick cannot be incremented", "physics.world3d.simulationTick"));
    }
    auto duration = eve::Duration::fromSeconds(static_cast<double>(std::min(dt, 0.05f)));
    if (!duration) return eve::Result<eve::SimulationStep>::failure(duration.status());
    return eve::Result<eve::SimulationStep>::success({*nextTick, std::move(duration).takeValue()});
}

struct WorldRegistry {
    std::mutex mutex;
    std::unordered_map<PhysicsWorldHandle::index_type, World3D*> worlds;
};
WorldRegistry& worldRegistry() {
    static auto* registry = new WorldRegistry;
    return *registry;
}

}  // namespace

eve::Result<std::unique_ptr<World3D>> World3D::createWithBackend(RigidBackend3D backend, float gravityX, float gravityY,
                                                                 float gravityZ, bool sleep,
                                                                 eve::PersistentId instanceId) {
    if (backend != RigidBackend3D::Box3D
#if EVENGINE_HAS_JOLT
        && backend != RigidBackend3D::Jolt
#endif
    )
        return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "Requested rigid backend is not available in this build",
            "physics.world3d.backend"));
    if (!std::isfinite(gravityX) || !std::isfinite(gravityY) || !std::isfinite(gravityZ))
        return eve::Result<std::unique_ptr<World3D>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Gravity must be finite", "physics.world3d.gravity"));
    try {
        return eve::Result<std::unique_ptr<World3D>>::success(
            std::unique_ptr<World3D>(new World3D(gravityX, gravityY, gravityZ, sleep, instanceId, true, backend)));
    } catch (const eve::Exception &error) {
        return eve::Result<std::unique_ptr<World3D>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "physics.world3d.create"));
    }
}

World3D::World3D(float gravityX, float gravityY, float gravityZ, bool sleep, eve::PersistentId instanceId)
    : World3D(gravityX, gravityY, gravityZ, sleep, instanceId, true, RigidBackend3D::Box3D) {}

World3D::World3D(float gravityX, float gravityY, float gravityZ, bool sleep, eve::PersistentId instanceId,
                 bool registerQueries, RigidBackend3D backend)
    : rigidBackendKind_(backend), instanceId_(instanceId) {
    runtimeHandle_ = detail::allocatePhysicsWorldHandle();
    if (instanceId_.isNil()) instanceId_ = detail::makePhysicsWorldPersistentId(runtimeHandle_);
#if EVENGINE_HAS_JOLT
    if (backend == RigidBackend3D::Jolt)
        rigidBackend_ = worldValue(jolt::makeWorld(*this, gravityX, gravityY, gravityZ, sleep));
    else
#endif
        rigidBackend_ = worldValue(box3d::makeWorld(*this, gravityX, gravityY, gravityZ, sleep));
    auto selection = rigidBackend_->selectSimulation();
    if (!selection) {
        const eve::Status status = selection.status();
        throw eve::Exception("World3D: cannot select a simulation backend: %s", status.describe().c_str());
    }
    backendSelectionStatus_ = selection.status();
    auto selected           = std::move(selection).takeValue();
    backendFallback_        = selected.usedFallback;
    simulation_             = std::move(selected.backend);
    if (registerQueries) registerCameraObstructionWorld(this);
    auto targetingRegistration = registerQueries ? registerTargetingLineOfSightWorld(this)
                                                  : eve::Result<void>::success();
    if (!registerQueries) {
        targetingRegistration.ignore("detached topology candidate does not publish targeting queries");
    } else if (!targetingRegistration) {
        // Physics supports multiple worlds; targeting LOS deliberately exposes
        // only one, so an explicitly consumed Conflict leaves this world valid
        // without making its query context ambiguous.
        targetingRegistration.ignore("another World3D is already the active targeting LOS world");
    }
    auto& registry = worldRegistry();
    std::scoped_lock registryLock(registry.mutex);
    registry.worlds[runtimeHandle_.index()] = this;
}

void World3D::setContinuousCollisionEnabled(bool enabled) {
    worldValue(rigidBackend_->setContinuousCollisionEnabled(enabled));
}


bool World3D::isContinuousCollisionEnabled() const { return worldValue(rigidBackend_->isContinuousCollisionEnabled()); }


void World3D::setRestitutionThreshold(float speed) { worldValue(rigidBackend_->setRestitutionThreshold(speed)); }


float World3D::getRestitutionThreshold() const { return worldValue(rigidBackend_->getRestitutionThreshold()); }


void World3D::setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) {
    worldValue(rigidBackend_->setContactTuning(hertz, dampingRatio, pushOutSpeed));
}


void World3D::setContactRecycleDistance(float distance) {
    worldValue(rigidBackend_->setContactRecycleDistance(distance));
}


float World3D::getContactRecycleDistance() const { return worldValue(rigidBackend_->getContactRecycleDistance()); }


void World3D::setMaximumLinearSpeed(float speed) { worldValue(rigidBackend_->setMaximumLinearSpeed(speed)); }


float World3D::getMaximumLinearSpeed() const { return worldValue(rigidBackend_->getMaximumLinearSpeed()); }


void World3D::setWarmStartingEnabled(bool enabled) { worldValue(rigidBackend_->setWarmStartingEnabled(enabled)); }


bool World3D::isWarmStartingEnabled() const { return worldValue(rigidBackend_->isWarmStartingEnabled()); }


int World3D::explode(float x, float y, float z, float radius, float falloff,
                     float impulsePerArea, int maskBits) {
    return worldValue(rigidBackend_->explode(x, y, z, radius, falloff, impulsePerArea, maskBits));
}


const World3D::ExplosionResult &World3D::explosionResultAt(int index,
                                                           const char *operation) const {
    if (index < 0 || index >= static_cast<int>(explosionResults_.size()))
        throw eve::Exception("World3D.%s: index out of range", operation);
    return explosionResults_[static_cast<size_t>(index)];
}

#define EV_EXPLOSION_INT_GETTER(name, member)                                      \
    int World3D::name(int index) const { return explosionResultAt(index, #name).member; }

EV_EXPLOSION_INT_GETTER(getExplosionResultBodyId, bodyId)
#undef EV_EXPLOSION_INT_GETTER

#define EV_EXPLOSION_FLOAT_GETTER(name, member)                                    \
    float World3D::name(int index) const { return explosionResultAt(index, #name).member; }

EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityX, deltaVX)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityY, deltaVY)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaVelocityZ, deltaVZ)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityX, deltaWX)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityY, deltaWY)
EV_EXPLOSION_FLOAT_GETTER(getExplosionResultDeltaAngularVelocityZ, deltaWZ)
#undef EV_EXPLOSION_FLOAT_GETTER

float World3D::getBoundsMinX() const { return worldValue(rigidBackend_->getBoundsMinX()); }

float World3D::getBoundsMinY() const { return worldValue(rigidBackend_->getBoundsMinY()); }

float World3D::getBoundsMinZ() const { return worldValue(rigidBackend_->getBoundsMinZ()); }

float World3D::getBoundsMaxX() const { return worldValue(rigidBackend_->getBoundsMaxX()); }

float World3D::getBoundsMaxY() const { return worldValue(rigidBackend_->getBoundsMaxY()); }

float World3D::getBoundsMaxZ() const { return worldValue(rigidBackend_->getBoundsMaxZ()); }


int World3D::getBodyCount() const { return worldValue(rigidBackend_->getBodyCount()); }

int World3D::getShapeCount() const { return worldValue(rigidBackend_->getShapeCount()); }

int World3D::getContactCount() const { return worldValue(rigidBackend_->getContactCount()); }

int World3D::getJointCount() const { return worldValue(rigidBackend_->getJointCount()); }

int World3D::getIslandCount() const { return worldValue(rigidBackend_->getIslandCount()); }

int World3D::getAwakeContactCount() const { return worldValue(rigidBackend_->getAwakeContactCount()); }

int World3D::getRecycledContactCount() const { return worldValue(rigidBackend_->getRecycledContactCount()); }

int World3D::getStaticTreeHeight() const { return worldValue(rigidBackend_->getStaticTreeHeight()); }

int World3D::getDynamicTreeHeight() const { return worldValue(rigidBackend_->getDynamicTreeHeight()); }

int World3D::getMemoryByteCount() const { return worldValue(rigidBackend_->getMemoryByteCount()); }


int World3D::getAwakeBodyCount() const { return worldValue(rigidBackend_->getAwakeBodyCount()); }


float World3D::getProfileStepMs() const { return worldValue(rigidBackend_->getProfileStepMs()); }

float World3D::getProfilePairsMs() const { return worldValue(rigidBackend_->getProfilePairsMs()); }

float World3D::getProfileCollideMs() const { return worldValue(rigidBackend_->getProfileCollideMs()); }

float World3D::getProfileSolveMs() const { return worldValue(rigidBackend_->getProfileSolveMs()); }

float World3D::getProfileBulletsMs() const { return worldValue(rigidBackend_->getProfileBulletsMs()); }

float World3D::getProfileSensorsMs() const { return worldValue(rigidBackend_->getProfileSensorsMs()); }


World3D::~World3D() { destroy(); }

void World3D::adoptPreparedTopology(World3D &prepared) {
    worldValue(rigidBackend_->swapTopology(*prepared.rigidBackend_));
    std::swap(nextId_, prepared.nextId_);
    std::swap(nextShapeId_, prepared.nextShapeId_);
    std::swap(nextJointId_, prepared.nextJointId_);
    std::swap(bodies_, prepared.bodies_);
    std::swap(shapes_, prepared.shapes_);
    std::swap(joints_, prepared.joints_);
    std::swap(shapeRecords_, prepared.shapeRecords_);
    std::swap(shapeHandles_, prepared.shapeHandles_);
    std::swap(shapeRawHandles_, prepared.shapeRawHandles_);
    std::swap(jointHandles_, prepared.jointHandles_);
    std::swap(simulationTick_, prepared.simulationTick_);
    for (Body3D *body : bodies_) body->world_ = this;
    for (Shape3D *shape : shapes_) shape->world_ = this;
    for (Joint3D *joint : joints_) joint->world_ = this;
    for (Body3D *body : prepared.bodies_) body->world_ = &prepared;
    for (Shape3D *shape : prepared.shapes_) shape->world_ = &prepared;
    for (Joint3D *joint : prepared.joints_) joint->world_ = &prepared;
}

bool World3D::isValid() const { return !destroyed_ && rigidBackend_ && rigidBackend_->isValid(); }

void World3D::destroy() {
    if (destroyed_) return;
    {
        auto& registry = worldRegistry();
    std::scoped_lock registryLock(registry.mutex);
        auto found = registry.worlds.find(runtimeHandle_.index());
        if (found != registry.worlds.end() && found->second == this) registry.worlds.erase(found);
    }
    destroyed_ = true;
    // Invalidate borrowed query registrations without touching any global
    // registry. This keeps teardown safe for every static destruction order.
    queryLifetime_.reset();

    std::vector<Mechanism3D *> mechanisms(mechanisms_.begin(), mechanisms_.end());
    for (Mechanism3D *mechanism : mechanisms) {
        if (mechanism) mechanism->invalidate();
    }
    mechanisms_.clear();

    std::vector<Joint3D *> joints(joints_.begin(), joints_.end());
    for (Joint3D *joint : joints) {
        if (joint) joint->invalidate();
    }
    joints_.clear();
    disabledBodyPairs_.clear();
    disabledShapePairs_.clear();

    std::vector<Body3D *> bodies(bodies_.begin(), bodies_.end());
    for (Body3D *b : bodies) {
        if (b) b->invalidate();
    }
    bodies_.clear();

    std::vector<Shape3D *> shapes(shapes_.begin(), shapes_.end());
    for (Shape3D *s : shapes) {
        if (s) s->invalidate();
    }
    shapes_.clear();

    simulation_.reset();
    if (rigidBackend_) rigidBackend_->destroy();
    clearContactEvents();
    shapeRecords_.clear();
    shapeHandles_.clear();
    shapeRawHandles_.clear();
    jointHandles_.clear();
    runtimeHandle_ = PhysicsWorldHandle::invalid();
}

World3D* World3D::findWorld(PhysicsWorldHandle handle) noexcept {
    if (handle.isInvalid()) return nullptr;
    auto& registry = worldRegistry();
    std::scoped_lock registryLock(registry.mutex);
    auto found = registry.worlds.find(handle.index());
    if (found == registry.worlds.end() || !found->second || found->second->runtimeHandle() != handle) return nullptr;
    return found->second;
}

void World3D::forgetJoint(Joint3D *joint) {
    if (!joint) return;
    joints_.erase(joint);
    if (joint->runtimeHandle().isValid()) jointHandles_.erase(joint->runtimeHandle());
    for (auto it = jointHandles_.begin(); it != jointHandles_.end();) {
        if (it->second == joint)
            it = jointHandles_.erase(it);
        else
            ++it;
    }
}

void World3D::update(float dt) { updateFull(dt, 4); }

void World3D::updateFull(float dt, int subStepCount) {
    EV_PROFILE_MODULE("physics", "World3D::update");
    auto legacyStep = makeLegacyStep(dt, simulationTick_);
    if (!legacyStep) {
        legacyStep.ignore("legacy World3D::updateFull cannot return a structured error");
        return;
    }
    auto result = step(std::move(legacyStep).takeValue(), SimulationSettings{8, 3, subStepCount});
    result.ignore("legacy World3D::updateFull cannot return a structured result");
}

eve::Result<void> World3D::step(const eve::SimulationStep &stepValue, const SimulationSettings &settings) {
    if (!isValid() || !simulation_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot step a destroyed or uninitialized 3D physics world",
            "physics.world3d.step"));
    }
    auto valid = detail::validateSimulationStep(stepValue, settings, simulation_->observation());
    if (!valid) return valid;
    for (Mechanism3D *mechanism : mechanisms_) {
        if (mechanism) mechanism->syncBeforeStep();
    }
    for (Shape3D *shape : shapes_) {
        if (shape && shape->isOneWay()) shape->refreshOneWayWorldData();
    }
    auto result = simulation_->step(stepValue, settings);
    if (!result) return result;
    simulationTick_ = stepValue.tick;
    clearContactEvents();
    emitContactEvents();
    return result;
}

SimulationObservation World3D::simulationObservation() const noexcept {
    return simulation_ ? simulation_->observation() : SimulationObservation{};
}

SimulationBackendKind World3D::backendKind() const noexcept {
    return simulation_ ? simulation_->kind() : SimulationBackendKind::Cpu;
}

SimulationDeterminism World3D::backendDeterminism() const noexcept {
    return simulation_ ? simulation_->determinism() : SimulationDeterminism::ToleranceBounded;
}

eve::Status World3D::backendSelectionStatus() const { return backendSelectionStatus_; }

void World3D::setGravity(float gx, float gy, float gz) { worldValue(rigidBackend_->setGravity(gx, gy, gz)); }


float World3D::getGravityX() const { return worldValue(rigidBackend_->getGravityX()); }


float World3D::getGravityY() const { return worldValue(rigidBackend_->getGravityY()); }


float World3D::getGravityZ() const { return worldValue(rigidBackend_->getGravityZ()); }


void World3D::setHitEventThreshold(float speed) { worldValue(rigidBackend_->setHitEventThreshold(speed)); }


float World3D::getHitEventThreshold() const { return worldValue(rigidBackend_->getHitEventThreshold()); }


void World3D::refreshContactFilter(Shape3D *shape) {
    if (!shape || !shape->isValid()) return;
    shape->setCategoryBits(shape->getCategoryBits());
}

void World3D::setBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB, bool enabled) {
    if (!bodyA || !bodyB || bodyA == bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this)
        throw eve::Exception(
            "World3D.setBodyPairCollisionEnabled: distinct live bodies must belong to this world");
    const uint64_t key = stablePairKey(bodyA->getId(), bodyB->getId());
    for (Shape3D *shape : shapes_) {
        if (shape && (shape->getBody() == bodyA || shape->getBody() == bodyB))
            shape->enableContactOverrideFiltering();
    }
    if (enabled) disabledBodyPairs_.erase(key); else disabledBodyPairs_.insert(key);
    for (Shape3D *shape : shapes_) {
        if (shape && (shape->getBody() == bodyA || shape->getBody() == bodyB))
            refreshContactFilter(shape);
    }
}

bool World3D::isBodyPairCollisionEnabled(Body3D *bodyA, Body3D *bodyB) const {
    if (!bodyA || !bodyB || bodyA == bodyB || !bodyA->isValid() || !bodyB->isValid() ||
        bodyA->getWorld() != this || bodyB->getWorld() != this)
        throw eve::Exception(
            "World3D.isBodyPairCollisionEnabled: distinct live bodies must belong to this world");
    return disabledBodyPairs_.count(stablePairKey(bodyA->getId(), bodyB->getId())) == 0;
}

void World3D::setShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB, bool enabled) {
    if (!shapeA || !shapeB || shapeA == shapeB || !shapeA->isValid() || !shapeB->isValid() ||
        !shapeA->getBody() || !shapeB->getBody() ||
        shapeA->getBody()->getWorld() != this || shapeB->getBody()->getWorld() != this)
        throw eve::Exception(
            "World3D.setShapePairCollisionEnabled: distinct live shapes must belong to this world");
    const uint64_t key = stablePairKey(shapeA->getId(), shapeB->getId());
    shapeA->enableContactOverrideFiltering();
    shapeB->enableContactOverrideFiltering();
    if (enabled) disabledShapePairs_.erase(key); else disabledShapePairs_.insert(key);
    refreshContactFilter(shapeA);
    refreshContactFilter(shapeB);
}

bool World3D::isShapePairCollisionEnabled(Shape3D *shapeA, Shape3D *shapeB) const {
    if (!shapeA || !shapeB || shapeA == shapeB || !shapeA->isValid() || !shapeB->isValid() ||
        !shapeA->getBody() || !shapeB->getBody() ||
        shapeA->getBody()->getWorld() != this || shapeB->getBody()->getWorld() != this)
        throw eve::Exception(
            "World3D.isShapePairCollisionEnabled: distinct live shapes must belong to this world");
    return disabledShapePairs_.count(stablePairKey(shapeA->getId(), shapeB->getId())) == 0;
}


void World3D::removeCollisionOverridesForBody(int bodyId) {
    for (auto it = disabledBodyPairs_.begin(); it != disabledBodyPairs_.end();) {
        if (pairContains(*it, bodyId)) it = disabledBodyPairs_.erase(it); else ++it;
    }
}

void World3D::removeCollisionOverridesForShape(int shapeId) {
    for (auto it = disabledShapePairs_.begin(); it != disabledShapePairs_.end();) {
        if (pairContains(*it, shapeId)) it = disabledShapePairs_.erase(it); else ++it;
    }
}

int World3D::nextBodyId() { return nextId_++; }

PhysicsBodyHandle World3D::nextBodyRuntimeHandle() {
    if (nextBodyHandleIndex_ == PhysicsBodyHandle::invalidIndex)
        throw eve::Exception("World3D.newBody: process-local body handle space exhausted");
    return PhysicsBodyHandle(nextBodyHandleIndex_++, 1u);
}

PhysicsShapeHandle World3D::nextShapeRuntimeHandle() {
    if (nextShapeHandleIndex_ == PhysicsShapeHandle::invalidIndex)
        throw eve::Exception("Body3D.newShape: process-local shape handle space exhausted");
    return PhysicsShapeHandle(nextShapeHandleIndex_++, 1u);
}

PhysicsJointHandle World3D::nextJointRuntimeHandle() {
    if (nextJointHandleIndex_ == PhysicsJointHandle::invalidIndex)
        throw eve::Exception("World3D.newJoint: process-local joint handle space exhausted");
    return PhysicsJointHandle(nextJointHandleIndex_++, 1u);
}

Body3D *World3D::newBody(const std::string &bodyType, float x, float y, float z) {
    if (!isValid()) throw eve::Exception("World3D.newBody: world destroyed");
    const auto handle = nextBodyRuntimeHandle();
    auto       native = worldValue(rigidBackend_->createBody(bodyType, x, y, z));
    auto       body   = std::make_unique<Body3D>(this, std::move(native), nextBodyId(), handle);
    bodies_.insert(body.get());
    return body.release();
}

Body3D *World3D::findBody(PhysicsBodyHandle handle) const {
    if (!isValid() || handle.isInvalid()) return nullptr;
    for (Body3D *body : bodies_) {
        if (body && body->isValid() && body->runtimeHandle() == handle) return body;
    }
    return nullptr;
}

Body3D *World3D::findBodyById(int bodyId) const {
    if (!isValid() || bodyId < 0) return nullptr;
    for (Body3D *body : bodies_) {
        if (body && body->isValid() && body->getId() == bodyId) return body;
    }
    return nullptr;
}

Shape3D *World3D::findShape(PhysicsShapeHandle handle) const {
    if (!isValid() || handle.isInvalid()) return nullptr;
    const auto found = shapeHandles_.find(handle);
    if (found == shapeHandles_.end() || !found->second || !found->second->isValid()) return nullptr;
    return found->second;
}

Shape3D *World3D::findShapeById(int shapeId) const {
    if (!isValid() || shapeId < 0) return nullptr;
    for (Shape3D *shape : shapes_) {
        if (shape && shape->isValid() && shape->getId() == shapeId) return shape;
    }
    return nullptr;
}

Joint3D *World3D::findJoint(PhysicsJointHandle handle) const {
    if (!isValid() || handle.isInvalid()) return nullptr;
    const auto found = jointHandles_.find(handle);
    if (found == jointHandles_.end() || !found->second || !found->second->isValid()) return nullptr;
    return found->second;
}


void World3D::setQueryFilter(int categoryBits, int maskBits) {
    queryCategoryBits_ = static_cast<uint32_t>(categoryBits);
    queryMaskBits_ = static_cast<uint32_t>(maskBits);
}

void World3D::resetQueryFilter() {
    queryCategoryBits_ = 0xFFFFFFFFu;
    queryMaskBits_ = 0xFFFFFFFFu;
}

void World3D::setQueryIgnoredBodyId(int bodyId) {
    if (bodyId == 0 || bodyId < -1)
        throw eve::Exception("World3D.setQueryIgnoredBodyId: use a positive stable id or -1");
    queryIgnoredBodyId_ = bodyId;
}

void World3D::setQueryIgnoredShapeId(int shapeId) {
    if (shapeId == 0 || shapeId < -1)
        throw eve::Exception("World3D.setQueryIgnoredShapeId: use a positive stable id or -1");
    queryIgnoredShapeId_ = shapeId;
}

void World3D::clearQueryIgnores() {
    queryIgnoredBodyId_ = -1;
    queryIgnoredShapeId_ = -1;
}

void World3D::destroyBody(Body3D *body) {
    if (!body) return;
    body->destroy();
}

void World3D::forgetBody(Body3D *body) {
    if (body) removeCollisionOverridesForBody(body->getId());
    bodies_.erase(body);
}
void World3D::forgetShape(Shape3D *shape) {
    if (!shape) return;
    removeCollisionOverridesForShape(shape->getId());
    shapes_.erase(shape);
    const PhysicsShapeHandle handle = shape->runtimeHandle();
    if (handle.isValid()) shapeHandles_.erase(handle);
    for (auto it = shapeHandles_.begin(); it != shapeHandles_.end();) {
        if (it->second == shape)
            it = shapeHandles_.erase(it);
        else
            ++it;
    }
    for (auto it = shapeRawHandles_.begin(); it != shapeRawHandles_.end();) {
        if (it->second == handle)
            it = shapeRawHandles_.erase(it);
        else
            ++it;
    }
}

void World3D::registerShapeHandle(Shape3D *shape) {
    if (!shape || !shape->getBody() || !shape->isValid() || shape->runtimeHandle().isInvalid()) return;
    const PhysicsShapeHandle handle = shape->runtimeHandle();
    for (auto it = shapeHandles_.begin(); it != shapeHandles_.end();) {
        if (it->second == shape || it->first == handle)
            it = shapeHandles_.erase(it);
        else
            ++it;
    }
    for (auto it = shapeRawHandles_.begin(); it != shapeRawHandles_.end();) {
        if (it->second == handle)
            it = shapeRawHandles_.erase(it);
        else
            ++it;
    }
    indexShapeHandle(shape);
}

void World3D::indexShapeHandle(Shape3D *shape) {
    const auto     handle    = shape->runtimeHandle();
    const uint64_t rawKey    = rigidBackend_->shapeKey(*shape);
    shapeHandles_[handle]    = shape;
    shapeRawHandles_[rawKey] = handle;
    shapeRecords_[rawKey]    = EventShape{shape->getBody()->getId(), shape->getId(), shape->getTag()};
}


void World3D::updateShapeTag(Shape3D *shape) {
    if (!shape) return;
    for (auto &[key, record] : shapeRecords_) {
        (void)key;
        if (record.shapeId == shape->getId()) record.shapeTag = shape->getTag();
    }
}

void World3D::emitContactEvents() {
    if (!isValid()) return;
    const auto beginStart = beginContacts_.size(), endStart = endContacts_.size();
    const auto triggerBeginStart = beginTriggers_.size(), triggerEndStart = endTriggers_.size();
    const auto hitStart = hits_.size(), stressStart = jointStressEvents_.size();
    worldValue(rigidBackend_->collectEvents());

    auto *ev = eve::ModuleManager::getInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!ev) return;
    const auto publishPair = [&](const char *name, const EventShape &a, const EventShape &b) {
        std::vector<eve::platform_event::Variant> args = {
            eve::platform_event::Variant::makeInt(a.bodyId),   eve::platform_event::Variant::makeInt(b.bodyId),
            eve::platform_event::Variant::makeInt(a.shapeId),  eve::platform_event::Variant::makeInt(b.shapeId),
            eve::platform_event::Variant::makeInt(a.shapeTag), eve::platform_event::Variant::makeInt(b.shapeTag)};
        ev->push(new eve::platform_event::Message(name, args));
    };
    for (auto i = beginStart; i < beginContacts_.size(); ++i)
        publishPair("begincontact3d", beginContacts_[i].shapeA, beginContacts_[i].shapeB);
    for (auto i = endStart; i < endContacts_.size(); ++i)
        publishPair("endcontact3d", endContacts_[i].shapeA, endContacts_[i].shapeB);
    for (auto i = hitStart; i < hits_.size(); ++i) publishPair("hit3d", hits_[i].shapeA, hits_[i].shapeB);
    for (auto i = triggerBeginStart; i < beginTriggers_.size(); ++i)
        publishPair("begintrigger3d", beginTriggers_[i].sensor, beginTriggers_[i].visitor);
    for (auto i = triggerEndStart; i < endTriggers_.size(); ++i)
        publishPair("endtrigger3d", endTriggers_[i].sensor, endTriggers_[i].visitor);
    for (auto i = stressStart; i < jointStressEvents_.size(); ++i) {
        const auto                               &joint = jointStressEvents_[i];
        std::vector<eve::platform_event::Variant> args  = {
            eve::platform_event::Variant::makeInt(joint.jointId), eve::platform_event::Variant::makeInt(joint.bodyAId),
            eve::platform_event::Variant::makeInt(joint.bodyBId), eve::platform_event::Variant::makeInt(joint.kind)};
        ev->push(new eve::platform_event::Message("jointstress3d", args));
    }
}

}  // namespace eve::physics
