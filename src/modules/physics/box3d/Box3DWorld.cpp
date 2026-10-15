#include "physics/box3d/Box3DWorld.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/box3d/Box3DWorldFactory.h"
#include "physics/box3d/Box3DWorldInternal.h"
namespace eve::physics::box3d {
b3WorldId Access::worldId(const World3D &world) {
    auto *native = dynamic_cast<WorldBackend *>(world.rigidBackend_.get());
    if (!native) throw eve::Exception("Box3D world access requires a Box3D backend");
    return native->worldId_;
}

using namespace world3d_detail;
namespace {
constexpr uint64_t combineModeMask      = 0x7ull;
constexpr int      frictionModeShift    = 32;
constexpr int      restitutionModeShift = 35;

float combineMaterialValue(float a, uint64_t materialA, float b, uint64_t materialB, int shift, bool friction) {
    const uint64_t modeA = (materialA >> shift) & combineModeMask;
    const uint64_t modeB = (materialB >> shift) & combineModeMask;
    const uint64_t mode  = std::max(modeA, modeB);
    switch (mode) {
        case 1: return 0.5f * (a + b);
        case 2: return std::min(a, b);
        case 3: return a * b;
        case 4: return std::max(a, b);
        default: return friction ? std::sqrt(a * b) : std::max(a, b);
    }
}

float mixFriction(float a, uint64_t materialA, float b, uint64_t materialB) {
    return combineMaterialValue(a, materialA, b, materialB, frictionModeShift, true);
}

float mixRestitution(float a, uint64_t materialA, float b, uint64_t materialB) {
    return combineMaterialValue(a, materialA, b, materialB, restitutionModeShift, false);
}

uint64_t stablePairKey(int idA, int idB) {
    const uint32_t low  = static_cast<uint32_t>(std::min(idA, idB));
    const uint32_t high = static_cast<uint32_t>(std::max(idA, idB));
    return (static_cast<uint64_t>(high) << 32) | low;
}

b3BodyType parseBodyType(const std::string &type) {
    if (type == "static") return b3_staticBody;
    if (type == "kinematic") return b3_kinematicBody;
    if (type == "dynamic") return b3_dynamicBody;
    throw eve::Exception("World3D.newBody: unknown body type '%s' (use static|kinematic|dynamic)", type.c_str());
}

void stepBox3D(void *context, const eve::SimulationStep &step, const SimulationSettings &settings) noexcept {
    auto *worldId = static_cast<b3WorldId *>(context);
    b3World_Step(*worldId, static_cast<float>(step.delta.seconds()), settings.subStepCount);
}


}  // namespace

WorldBackend::WorldBackend(World3D &owner, float gx, float gy, float gz, bool sleep) : owner_(&owner) {
    auto def                     = b3DefaultWorldDef();
    def.gravity                  = {gx, gy, gz};
    def.enableSleep              = sleep;
    owner_->contactHertz_        = def.contactHertz;
    owner_->contactDampingRatio_ = def.contactDampingRatio;
    owner_->contactPushOutSpeed_ = def.contactSpeed;
    worldId_                     = b3CreateWorld(&def);
    if (!b3World_IsValid(worldId_)) throw eve::Exception("Box3D rejected world creation");
    b3World_SetFrictionCallback(worldId_, &mixFriction);
    b3World_SetRestitutionCallback(worldId_, &mixRestitution);
    b3World_SetCustomFilterCallback(worldId_, &WorldBackend::do_customFilterCallback, this);
    b3World_SetPreSolveCallback(worldId_, &WorldBackend::do_preSolveCallback, this);
}
WorldBackend::~WorldBackend() { destroy(); }
bool WorldBackend::isValid() const noexcept { return b3World_IsValid(worldId_); }
void WorldBackend::destroy() noexcept {
    if (isValid()) b3DestroyWorld(worldId_);
    worldId_ = {};
}
eve::Result<SimulationBackendSelection> WorldBackend::selectSimulation() {
    return detail::selectSimulationBackend(
        SimulationBackendDomain::World3D,
        detail::makeCallbackSimulationBackend(&worldId_, &stepBox3D, SimulationBackendKind::Cpu,
                                              SimulationDeterminism::ToleranceBounded),
        &worldId_, true);
}
eve::Result<void> WorldBackend::swapTopology(IWorldBackend3D &prepared) {
    auto *source = dynamic_cast<WorldBackend *>(&prepared);
    if (!source)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "Cannot swap topology across rigid backends"));
    std::swap(worldId_, source->worldId_);
    b3World_SetCustomFilterCallback(worldId_, &WorldBackend::do_customFilterCallback, this);
    b3World_SetPreSolveCallback(worldId_, &WorldBackend::do_preSolveCallback, this);
    b3World_SetCustomFilterCallback(source->worldId_, &WorldBackend::do_customFilterCallback, source);
    b3World_SetPreSolveCallback(source->worldId_, &WorldBackend::do_preSolveCallback, source);
    return eve::Result<void>::success();
}
uint64_t WorldBackend::shapeKey(const Shape3D &shape) const { return b3StoreShapeId(Access::shapeId(shape)); }
eve::Result<std::unique_ptr<IBodyBackend3D>> WorldBackend::createBody(std::string_view type, float x, float y,
                                                                      float z) {
    return checked("physics.world3d.createBody", [&] {
        using R = eve::Result<std::unique_ptr<IBodyBackend3D>>;
        if (!isValid())
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "World is no longer valid"));
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return R::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Body position must be finite"));
        auto def     = b3DefaultBodyDef();
        def.type     = parseBodyType(std::string(type));
        def.position = {x, y, z};
        auto id      = b3CreateBody(worldId_, &def);
        if (!b3Body_IsValid(id))
            return R::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "Box3D rejected body creation"));
        return R::success(adoptBody(id));
    });
}
eve::Result<std::unique_ptr<IWorldBackend3D>> makeWorld(World3D &owner, float gx, float gy, float gz, bool sleep) {
    try {
        return eve::Result<std::unique_ptr<IWorldBackend3D>>::success(
            std::make_unique<WorldBackend>(owner, gx, gy, gz, sleep));
    } catch (const eve::Exception &error) {
        return eve::Result<std::unique_ptr<IWorldBackend3D>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, error.what(), "physics.world3d.create"));
    }
}

void WorldBackend::do_setContinuousCollisionEnabled(bool enabled) {
    if (isValid()) b3World_EnableContinuous(worldId_, enabled);
}

eve::Result<void> WorldBackend::setContinuousCollisionEnabled(bool enabled) {
    return checked("physics.world3d.setContinuousCollisionEnabled", [&] {
        do_setContinuousCollisionEnabled(enabled);
        return eve::Result<void>::success();
    });
}

bool WorldBackend::do_isContinuousCollisionEnabled() const {
    return isValid() && b3World_IsContinuousEnabled(worldId_);
}

eve::Result<bool> WorldBackend::isContinuousCollisionEnabled() const {
    return checked("physics.world3d.isContinuousCollisionEnabled",
                   [&] { return eve::Result<bool>::success(do_isContinuousCollisionEnabled()); });
}

void WorldBackend::do_setRestitutionThreshold(float speed) {
    if (!std::isfinite(speed) || speed < 0.f)
        throw eve::Exception("World3D.setRestitutionThreshold: speed must be finite and >= 0");
    if (isValid()) b3World_SetRestitutionThreshold(worldId_, speed);
}

eve::Result<void> WorldBackend::setRestitutionThreshold(float speed) {
    return checked("physics.world3d.setRestitutionThreshold", [&] {
        do_setRestitutionThreshold(speed);
        return eve::Result<void>::success();
    });
}

float WorldBackend::do_getRestitutionThreshold() const {
    return isValid() ? b3World_GetRestitutionThreshold(worldId_) : 0.f;
}

eve::Result<float> WorldBackend::getRestitutionThreshold() const {
    return checked("physics.world3d.getRestitutionThreshold",
                   [&] { return eve::Result<float>::success(do_getRestitutionThreshold()); });
}

void WorldBackend::do_setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) {
    if (!std::isfinite(hertz) || !(hertz > 0.f))
        throw eve::Exception("World3D.setContactTuning: hertz must be finite and > 0");
    if (!std::isfinite(dampingRatio) || dampingRatio < 0.f)
        throw eve::Exception("World3D.setContactTuning: dampingRatio must be finite and >= 0");
    if (!std::isfinite(pushOutSpeed) || pushOutSpeed < 0.f)
        throw eve::Exception("World3D.setContactTuning: pushOutSpeed must be finite and >= 0");
    owner_->contactHertz_        = hertz;
    owner_->contactDampingRatio_ = dampingRatio;
    owner_->contactPushOutSpeed_ = pushOutSpeed;
    if (isValid()) b3World_SetContactTuning(worldId_, hertz, dampingRatio, pushOutSpeed);
}

eve::Result<void> WorldBackend::setContactTuning(float hertz, float dampingRatio, float pushOutSpeed) {
    return checked("physics.world3d.setContactTuning", [&] {
        do_setContactTuning(hertz, dampingRatio, pushOutSpeed);
        return eve::Result<void>::success();
    });
}

void WorldBackend::do_setContactRecycleDistance(float distance) {
    if (!std::isfinite(distance) || distance < 0.f)
        throw eve::Exception("World3D.setContactRecycleDistance: distance must be finite and >= 0");
    if (isValid()) b3World_SetContactRecycleDistance(worldId_, distance);
}

eve::Result<void> WorldBackend::setContactRecycleDistance(float distance) {
    return checked("physics.world3d.setContactRecycleDistance", [&] {
        do_setContactRecycleDistance(distance);
        return eve::Result<void>::success();
    });
}

float WorldBackend::do_getContactRecycleDistance() const {
    return isValid() ? b3World_GetContactRecycleDistance(worldId_) : 0.f;
}

eve::Result<float> WorldBackend::getContactRecycleDistance() const {
    return checked("physics.world3d.getContactRecycleDistance",
                   [&] { return eve::Result<float>::success(do_getContactRecycleDistance()); });
}

void WorldBackend::do_setMaximumLinearSpeed(float speed) {
    if (!std::isfinite(speed) || !(speed > 0.f))
        throw eve::Exception("World3D.setMaximumLinearSpeed: speed must be finite and > 0");
    if (isValid()) b3World_SetMaximumLinearSpeed(worldId_, speed);
}

eve::Result<void> WorldBackend::setMaximumLinearSpeed(float speed) {
    return checked("physics.world3d.setMaximumLinearSpeed", [&] {
        do_setMaximumLinearSpeed(speed);
        return eve::Result<void>::success();
    });
}

float WorldBackend::do_getMaximumLinearSpeed() const {
    return isValid() ? b3World_GetMaximumLinearSpeed(worldId_) : 0.f;
}

eve::Result<float> WorldBackend::getMaximumLinearSpeed() const {
    return checked("physics.world3d.getMaximumLinearSpeed",
                   [&] { return eve::Result<float>::success(do_getMaximumLinearSpeed()); });
}

void WorldBackend::do_setWarmStartingEnabled(bool enabled) {
    if (isValid()) b3World_EnableWarmStarting(worldId_, enabled);
}

eve::Result<void> WorldBackend::setWarmStartingEnabled(bool enabled) {
    return checked("physics.world3d.setWarmStartingEnabled", [&] {
        do_setWarmStartingEnabled(enabled);
        return eve::Result<void>::success();
    });
}

bool WorldBackend::do_isWarmStartingEnabled() const { return isValid() && b3World_IsWarmStartingEnabled(worldId_); }

eve::Result<bool> WorldBackend::isWarmStartingEnabled() const {
    return checked("physics.world3d.isWarmStartingEnabled",
                   [&] { return eve::Result<bool>::success(do_isWarmStartingEnabled()); });
}

int WorldBackend::do_explode(float x, float y, float z, float radius, float falloff, float impulsePerArea,
                             int maskBits) {
    owner_->explosionResults_.clear();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw eve::Exception("World3D.explode: center must be finite");
    if (!std::isfinite(radius) || radius < 0.f || !std::isfinite(falloff) || falloff < 0.f)
        throw eve::Exception("World3D.explode: radius and falloff must be finite and >= 0");
    if (!std::isfinite(impulsePerArea)) throw eve::Exception("World3D.explode: impulsePerArea must be finite");
    if (!isValid() || impulsePerArea == 0.f) return 0;

    struct Before {
        Body3D *body = nullptr;
        b3Vec3  linear{};
        b3Vec3  angular{};
    };
    std::vector<Before> before;
    before.reserve(owner_->bodies_.size());
    for (Body3D *body : owner_->bodies_) {
        if (!body || !body->isValid() || b3Body_GetType(box3d::Access::bodyId(*body)) != b3_dynamicBody) continue;
        before.push_back({body, b3Body_GetLinearVelocity(box3d::Access::bodyId(*body)),
                          b3Body_GetAngularVelocity(box3d::Access::bodyId(*body))});
    }

    b3ExplosionDef def = b3DefaultExplosionDef();
    def.position       = b3Pos{x, y, z};
    def.radius         = radius;
    def.falloff        = falloff;
    def.impulsePerArea = impulsePerArea;
    def.maskBits       = static_cast<uint32_t>(maskBits);
    b3World_Explode(worldId_, &def);

    for (const Before &entry : before) {
        if (!entry.body->isValid()) continue;
        const b3Vec3 linear  = b3Body_GetLinearVelocity(box3d::Access::bodyId(*entry.body));
        const b3Vec3 angular = b3Body_GetAngularVelocity(box3d::Access::bodyId(*entry.body));
        const b3Vec3 dv      = linear - entry.linear;
        const b3Vec3 dw      = angular - entry.angular;
        if (b3LengthSquared(dv) <= 1e-12f && b3LengthSquared(dw) <= 1e-12f) continue;
        owner_->explosionResults_.push_back({entry.body->getId(), dv.x, dv.y, dv.z, dw.x, dw.y, dw.z});
    }
    std::sort(owner_->explosionResults_.begin(), owner_->explosionResults_.end(),
              [](const ExplosionResult &a, const ExplosionResult &b) { return a.bodyId < b.bodyId; });
    return static_cast<int>(owner_->explosionResults_.size());
}

eve::Result<int> WorldBackend::explode(float x, float y, float z, float radius, float falloff, float impulsePerArea,
                                       int maskBits) {
    return checked("physics.world3d.explode", [&] {
        return eve::Result<int>::success(do_explode(x, y, z, radius, falloff, impulsePerArea, maskBits));
    });
}

float WorldBackend::do_getBoundsMinX() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.x) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMinX() const {
    return checked("physics.world3d.getBoundsMinX", [&] { return eve::Result<float>::success(do_getBoundsMinX()); });
}

float WorldBackend::do_getBoundsMinY() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.y) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMinY() const {
    return checked("physics.world3d.getBoundsMinY", [&] { return eve::Result<float>::success(do_getBoundsMinY()); });
}

float WorldBackend::do_getBoundsMinZ() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).lowerBound.z) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMinZ() const {
    return checked("physics.world3d.getBoundsMinZ", [&] { return eve::Result<float>::success(do_getBoundsMinZ()); });
}

float WorldBackend::do_getBoundsMaxX() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.x) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMaxX() const {
    return checked("physics.world3d.getBoundsMaxX", [&] { return eve::Result<float>::success(do_getBoundsMaxX()); });
}

float WorldBackend::do_getBoundsMaxY() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.y) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMaxY() const {
    return checked("physics.world3d.getBoundsMaxY", [&] { return eve::Result<float>::success(do_getBoundsMaxY()); });
}

float WorldBackend::do_getBoundsMaxZ() const {
    return isValid() ? static_cast<float>(b3World_GetBounds(worldId_).upperBound.z) : 0.f;
}

eve::Result<float> WorldBackend::getBoundsMaxZ() const {
    return checked("physics.world3d.getBoundsMaxZ", [&] { return eve::Result<float>::success(do_getBoundsMaxZ()); });
}

int WorldBackend::do_getBodyCount() const { return isValid() ? b3World_GetCounters(worldId_).bodyCount : 0; }

eve::Result<int> WorldBackend::getBodyCount() const {
    return checked("physics.world3d.getBodyCount", [&] { return eve::Result<int>::success(do_getBodyCount()); });
}

int WorldBackend::do_getShapeCount() const { return isValid() ? b3World_GetCounters(worldId_).shapeCount : 0; }

eve::Result<int> WorldBackend::getShapeCount() const {
    return checked("physics.world3d.getShapeCount", [&] { return eve::Result<int>::success(do_getShapeCount()); });
}

int WorldBackend::do_getContactCount() const { return isValid() ? b3World_GetCounters(worldId_).contactCount : 0; }

eve::Result<int> WorldBackend::getContactCount() const {
    return checked("physics.world3d.getContactCount", [&] { return eve::Result<int>::success(do_getContactCount()); });
}

int WorldBackend::do_getJointCount() const { return isValid() ? b3World_GetCounters(worldId_).jointCount : 0; }

eve::Result<int> WorldBackend::getJointCount() const {
    return checked("physics.world3d.getJointCount", [&] { return eve::Result<int>::success(do_getJointCount()); });
}

int WorldBackend::do_getIslandCount() const { return isValid() ? b3World_GetCounters(worldId_).islandCount : 0; }

eve::Result<int> WorldBackend::getIslandCount() const {
    return checked("physics.world3d.getIslandCount", [&] { return eve::Result<int>::success(do_getIslandCount()); });
}

int WorldBackend::do_getAwakeContactCount() const {
    return isValid() ? b3World_GetCounters(worldId_).awakeContactCount : 0;
}

eve::Result<int> WorldBackend::getAwakeContactCount() const {
    return checked("physics.world3d.getAwakeContactCount",
                   [&] { return eve::Result<int>::success(do_getAwakeContactCount()); });
}

int WorldBackend::do_getRecycledContactCount() const {
    return isValid() ? b3World_GetCounters(worldId_).recycledContactCount : 0;
}

eve::Result<int> WorldBackend::getRecycledContactCount() const {
    return checked("physics.world3d.getRecycledContactCount",
                   [&] { return eve::Result<int>::success(do_getRecycledContactCount()); });
}

int WorldBackend::do_getStaticTreeHeight() const {
    return isValid() ? b3World_GetCounters(worldId_).staticTreeHeight : 0;
}

eve::Result<int> WorldBackend::getStaticTreeHeight() const {
    return checked("physics.world3d.getStaticTreeHeight",
                   [&] { return eve::Result<int>::success(do_getStaticTreeHeight()); });
}

int WorldBackend::do_getDynamicTreeHeight() const { return isValid() ? b3World_GetCounters(worldId_).treeHeight : 0; }

eve::Result<int> WorldBackend::getDynamicTreeHeight() const {
    return checked("physics.world3d.getDynamicTreeHeight",
                   [&] { return eve::Result<int>::success(do_getDynamicTreeHeight()); });
}

int WorldBackend::do_getMemoryByteCount() const { return isValid() ? b3World_GetCounters(worldId_).byteCount : 0; }

eve::Result<int> WorldBackend::getMemoryByteCount() const {
    return checked("physics.world3d.getMemoryByteCount",
                   [&] { return eve::Result<int>::success(do_getMemoryByteCount()); });
}

int WorldBackend::do_getAwakeBodyCount() const { return isValid() ? b3World_GetAwakeBodyCount(worldId_) : 0; }

eve::Result<int> WorldBackend::getAwakeBodyCount() const {
    return checked("physics.world3d.getAwakeBodyCount",
                   [&] { return eve::Result<int>::success(do_getAwakeBodyCount()); });
}

float WorldBackend::do_getProfileStepMs() const { return isValid() ? b3World_GetProfile(worldId_).step : 0; }

eve::Result<float> WorldBackend::getProfileStepMs() const {
    return checked("physics.world3d.getProfileStepMs",
                   [&] { return eve::Result<float>::success(do_getProfileStepMs()); });
}

float WorldBackend::do_getProfilePairsMs() const { return isValid() ? b3World_GetProfile(worldId_).pairs : 0; }

eve::Result<float> WorldBackend::getProfilePairsMs() const {
    return checked("physics.world3d.getProfilePairsMs",
                   [&] { return eve::Result<float>::success(do_getProfilePairsMs()); });
}

float WorldBackend::do_getProfileCollideMs() const { return isValid() ? b3World_GetProfile(worldId_).collide : 0; }

eve::Result<float> WorldBackend::getProfileCollideMs() const {
    return checked("physics.world3d.getProfileCollideMs",
                   [&] { return eve::Result<float>::success(do_getProfileCollideMs()); });
}

float WorldBackend::do_getProfileSolveMs() const { return isValid() ? b3World_GetProfile(worldId_).solve : 0; }

eve::Result<float> WorldBackend::getProfileSolveMs() const {
    return checked("physics.world3d.getProfileSolveMs",
                   [&] { return eve::Result<float>::success(do_getProfileSolveMs()); });
}

float WorldBackend::do_getProfileBulletsMs() const { return isValid() ? b3World_GetProfile(worldId_).bullets : 0; }

eve::Result<float> WorldBackend::getProfileBulletsMs() const {
    return checked("physics.world3d.getProfileBulletsMs",
                   [&] { return eve::Result<float>::success(do_getProfileBulletsMs()); });
}

float WorldBackend::do_getProfileSensorsMs() const { return isValid() ? b3World_GetProfile(worldId_).sensors : 0; }

eve::Result<float> WorldBackend::getProfileSensorsMs() const {
    return checked("physics.world3d.getProfileSensorsMs",
                   [&] { return eve::Result<float>::success(do_getProfileSensorsMs()); });
}

void WorldBackend::do_setGravity(float gx, float gy, float gz) {
    if (!isValid()) return;
    b3World_SetGravity(worldId_, b3Vec3{gx, gy, gz});
}

eve::Result<void> WorldBackend::setGravity(float gx, float gy, float gz) {
    return checked("physics.world3d.setGravity", [&] {
        do_setGravity(gx, gy, gz);
        return eve::Result<void>::success();
    });
}

float WorldBackend::do_getGravityX() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).x;
}

eve::Result<float> WorldBackend::getGravityX() const {
    return checked("physics.world3d.getGravityX", [&] { return eve::Result<float>::success(do_getGravityX()); });
}

float WorldBackend::do_getGravityY() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).y;
}

eve::Result<float> WorldBackend::getGravityY() const {
    return checked("physics.world3d.getGravityY", [&] { return eve::Result<float>::success(do_getGravityY()); });
}

float WorldBackend::do_getGravityZ() const {
    if (!isValid()) return 0.f;
    return b3World_GetGravity(worldId_).z;
}

eve::Result<float> WorldBackend::getGravityZ() const {
    return checked("physics.world3d.getGravityZ", [&] { return eve::Result<float>::success(do_getGravityZ()); });
}

void WorldBackend::do_setHitEventThreshold(float speed) {
    if (!std::isfinite(speed) || speed < 0.f)
        throw eve::Exception("World3D.setHitEventThreshold: speed must be finite and >= 0");
    if (isValid()) b3World_SetHitEventThreshold(worldId_, speed);
}

eve::Result<void> WorldBackend::setHitEventThreshold(float speed) {
    return checked("physics.world3d.setHitEventThreshold", [&] {
        do_setHitEventThreshold(speed);
        return eve::Result<void>::success();
    });
}

float WorldBackend::do_getHitEventThreshold() const { return isValid() ? b3World_GetHitEventThreshold(worldId_) : 0.f; }

eve::Result<float> WorldBackend::getHitEventThreshold() const {
    return checked("physics.world3d.getHitEventThreshold",
                   [&] { return eve::Result<float>::success(do_getHitEventThreshold()); });
}

bool WorldBackend::do_customFilterCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, void *context) {
    auto    *world  = static_cast<WorldBackend *>(context);
    Shape3D *shapeA = shapeFromShape(shapeIdA);
    Shape3D *shapeB = shapeFromShape(shapeIdB);
    Body3D  *bodyA  = bodyFromShape(shapeIdA);
    Body3D  *bodyB  = bodyFromShape(shapeIdB);
    if (!world || !shapeA || !shapeB || !bodyA || !bodyB) return false;
    if (world->owner_->disabledBodyPairs_.count(stablePairKey(bodyA->getId(), bodyB->getId())) != 0) return false;
    return world->owner_->disabledShapePairs_.count(stablePairKey(shapeA->getId(), shapeB->getId())) == 0;
}

b3QueryFilter WorldBackend::do_makeQueryFilter() const {
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits  = owner_->queryCategoryBits_;
    filter.maskBits      = owner_->queryMaskBits_;
    return filter;
}

bool WorldBackend::do_shouldIgnoreQueryShape(b3ShapeId shapeId) const {
    Body3D  *body  = bodyFromShape(shapeId);
    Shape3D *shape = shapeFromShape(shapeId);
    if (!body || !shape) return true;
    return body->getId() == owner_->queryIgnoredBodyId_ || shape->getId() == owner_->queryIgnoredShapeId_;
}

bool WorldBackend::do_preSolveCallback(b3ShapeId shapeIdA, b3ShapeId shapeIdB, b3Pos point, b3Vec3 normal,
                                       void *context) {
    auto *world = static_cast<WorldBackend *>(context);
    if (!world) return true;
    auto findShape = [world](b3ShapeId id) -> Shape3D * {
        const auto raw = world->owner_->shapeRawHandles_.find(b3StoreShapeId(id));
        if (raw == world->owner_->shapeRawHandles_.end()) return nullptr;
        const auto found = world->owner_->shapeHandles_.find(raw->second);
        return found == world->owner_->shapeHandles_.end() ? nullptr : found->second;
    };
    Shape3D *shapeA = findShape(shapeIdA);
    Shape3D *shapeB = findShape(shapeIdB);
    if (shapeA && shapeA->isOneWay() &&
        !shapeA->allowsOneWayContact(point.x, point.y, point.z, normal.x, normal.y, normal.z))
        return false;
    if (shapeB && shapeB->isOneWay() &&
        !shapeB->allowsOneWayContact(point.x, point.y, point.z, -normal.x, -normal.y, -normal.z))
        return false;
    return true;
}

}  // namespace eve::physics::box3d
