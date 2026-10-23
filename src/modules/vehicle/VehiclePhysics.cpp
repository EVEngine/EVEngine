#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleTireRuntime.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace eve::vehicle {

class VehiclePhysicsBinding {
public:
    enum class Space : std::uint8_t { TwoD, ThreeD };
    std::shared_ptr<VehicleWheelRuntime> wheelRuntime;

    VehiclePhysicsBinding(eve::physics::PhysicsLink link, eve::physics::World* world,
                          std::weak_ptr<const void> lifetime)
        : link_(link), space_(Space::TwoD), world_(world), lifetime_(std::move(lifetime)) {}

    VehiclePhysicsBinding(eve::physics::PhysicsLink link, eve::physics::World3D* world,
                          std::weak_ptr<const void> lifetime)
        : link_(link), space_(Space::ThreeD), world_(world), lifetime_(std::move(lifetime)) {}

    ~VehiclePhysicsBinding() noexcept { detach(); }

    [[nodiscard]] eve::physics::Body* resolve2D() const {
        if (space_ != Space::TwoD) return nullptr;
        auto lifetime = lifetime_.lock();
        if (!lifetime) return nullptr;
        auto* world = static_cast<eve::physics::World*>(world_);
        if (!world) return nullptr;
        auto resolved = link_.resolve(*world);
        if (!resolved) {
            resolved.ignore("vehicle physics link became stale");
            return nullptr;
        }
        return std::move(resolved).takeValue();
    }

    [[nodiscard]] eve::physics::Body3D* resolve3D() const {
        if (space_ != Space::ThreeD) return nullptr;
        auto lifetime = lifetime_.lock();
        if (!lifetime) return nullptr;
        auto* world = static_cast<eve::physics::World3D*>(world_);
        if (!world) return nullptr;
        auto resolved = link_.resolve(*world);
        if (!resolved) {
            resolved.ignore("vehicle physics link became stale");
            return nullptr;
        }
        return std::move(resolved).takeValue();
    }

    [[nodiscard]] bool isAttached() const { return resolve2D() != nullptr || resolve3D() != nullptr; }

    void detach() noexcept {
        wheelRuntime.reset();
        if (auto* body = resolve2D()) body->destroy();
        if (auto* body = resolve3D()) body->destroy();
        link_     = {};
        world_    = nullptr;
        lifetime_.reset();
    }

private:
    eve::physics::PhysicsLink  link_;
    Space                      space_;
    void*                      world_ = nullptr;
    std::weak_ptr<const void>  lifetime_;
};

namespace {

constexpr float kPi = 3.14159265358979323846f;

float normalizeDeg(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}


void shipMove2D(VehicleEntity& v, eve::physics::Body* b, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    auto in = v.input();
    auto mo = v.motion();
    mo->x   = b->getX();
    mo->y   = b->getY();

    const float speedFactor = std::clamp(std::fabs(mo->speed) / def->maxSpeed, 0.f, 1.f);
    mo->heading = normalizeDeg(mo->heading + in->steer * def->turnRate * (0.35f + 0.65f * speedFactor) * dt);

    float target = in->throttle * def->maxSpeed;
    if (in->brake > 0.f || in->handbrake) target = 0.f;
    const float dv    = target - mo->speed;
    const float maxDv = def->accel * dt;
    mo->speed += std::clamp(dv, -maxDv, maxDv);

    const float rad = mo->heading * kPi / 180.f;
    b->setAngle(rad);
    b->setLinearVelocity(std::cos(rad) * mo->speed, std::sin(rad) * mo->speed);
}

void shipMove3D(VehicleEntity& v, eve::physics::Body3D* b, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    auto in = v.input();
    auto mo = v.motion();
    mo->x   = b->getX();
    mo->y   = b->getZ();

    const float speedFactor = std::clamp(std::fabs(mo->speed) / def->maxSpeed, 0.f, 1.f);
    mo->heading = normalizeDeg(mo->heading + in->steer * def->turnRate * (0.35f + 0.65f * speedFactor) * dt);

    float target = in->throttle * def->maxSpeed;
    if (in->brake > 0.f || in->handbrake) target = 0.f;
    const float dv    = target - mo->speed;
    const float maxDv = def->accel * dt;
    mo->speed += std::clamp(dv, -maxDv, maxDv);

    const float rad = mo->heading * kPi / 180.f;
    b->setRotation(0.f, std::sin(rad * 0.5f), 0.f, std::cos(rad * 0.5f));
    b->setLinearVelocity(std::sin(rad) * mo->speed, 0.f, std::cos(rad) * mo->speed);
}

class SuspensionMobility3D : public IVehicleMobility {
public:
    const char* name() const override { return "suspension"; }
    void        update(VehicleEntity& v, float dt) override {
        if (VehiclePhysics::tryWheelMove(v, dt) != VehiclePhysicsStatus::Unavailable) return;
        if (IVehicleMobility* kinematic = VehicleSystem::findMobility("kinematic")) {
            kinematic->update(v, dt);
        }
    }
};

}  // namespace

VehiclePhysicsStatus VehiclePhysics::attach2D(VehicleEntity* v, eve::physics::World* world) {
    if (v == nullptr || world == nullptr) return VehiclePhysicsStatus::Unavailable;
    (void)detach(v);
    const VehicleDefinition* def = v->definition()->def;
    if (def == nullptr) return VehiclePhysicsStatus::Unavailable;
    auto mo = v->motion();

    eve::physics::Body* b = world->newBody("dynamic", mo->x, mo->y);
    if (b == nullptr) return VehiclePhysicsStatus::Unavailable;
    b->newCircleFixture(def->radius, 1.f, 0.6f, 0.f);
    b->setAngle(mo->heading * kPi / 180.f);
    auto link = eve::physics::PhysicsLink::fromBody(*b);
    if (!link) {
        link.ignore("new vehicle body did not produce a valid physics link");
        b->destroy();
        return VehiclePhysicsStatus::Unavailable;
    }
    v->physicsBody()->binding = std::make_shared<VehiclePhysicsBinding>(
        std::move(link).takeValue(), world, world->lifetimeToken());
    v->physicsBody()->space = "2d";
    v->suspension()->wheels.clear();
    return VehiclePhysicsStatus::Applied;
}

VehiclePhysicsStatus VehiclePhysics::attach3D(VehicleEntity* v, eve::physics::World3D* world, float heightY) {
    if (v == nullptr || world == nullptr) return VehiclePhysicsStatus::Unavailable;
    (void)detach(v);
    const VehicleDefinition* def = v->definition()->def;
    if (def == nullptr) return VehiclePhysicsStatus::Unavailable;
    auto mo = v->motion();

    eve::physics::Body3D* b = world->newBody("dynamic", mo->x, heightY, mo->y);
    if (b == nullptr) return VehiclePhysicsStatus::Unavailable;
    eve::physics::Shape3D* shape = b->newBoxShape(def->radius, 0.35f, def->radius, 1.f, 0.8f, 0.f);
    if (shape != nullptr) shape->setFilterBits(2, ~uint64_t{0});
    const float rad = mo->heading * kPi / 180.f;
    b->setRotation(0.f, std::sin(rad * 0.5f), 0.f, std::cos(rad * 0.5f));
    b->setAwake(true);

    auto link = eve::physics::PhysicsLink::fromBody(*b);
    if (!link) {
        link.ignore("new vehicle body did not produce a valid physics link");
        b->destroy();
        return VehiclePhysicsStatus::Unavailable;
    }
    v->physicsBody()->binding = std::make_shared<VehiclePhysicsBinding>(
        std::move(link).takeValue(), world, world->lifetimeToken());
    v->physicsBody()->space = "3d";
    v->suspension()->wheels.assign(def->suspension.wheels.size(), {});
    return VehiclePhysicsStatus::Applied;
}

VehiclePhysicsStatus VehiclePhysics::detach(VehicleEntity* v) {
    if (v == nullptr) return VehiclePhysicsStatus::Unavailable;
    auto pb = v->physicsBody();
    const bool had = pb->binding != nullptr;
    if (pb->binding) pb->binding->detach();
    pb->binding.reset();
    pb->space.clear();
    return had ? VehiclePhysicsStatus::Applied : VehiclePhysicsStatus::Unavailable;
}

bool VehiclePhysics::isAttached(VehicleEntity* v) {
    return v != nullptr && v->physicsBody()->binding && v->physicsBody()->binding->isAttached();
}

float VehiclePhysics::height(VehicleEntity* v) {
    const auto binding = v == nullptr ? nullptr : v->physicsBody()->binding;
    if (auto* body = binding ? binding->resolve3D() : nullptr) return body->getY();
    return 0.f;
}

VehiclePhysicsStatus VehiclePhysics::tryWheelMove(VehicleEntity& v, float dt) {
    const auto binding = v.physicsBody()->binding;
    if (eve::physics::Body* b = binding ? binding->resolve2D() : nullptr) {
        if (const auto* definition = v.definition()->def; definition && definition->mobility == "ship") {
            shipMove2D(v, b, dt);
            return VehiclePhysicsStatus::Applied;
        }
        auto synchronized = synchronizeWheelState(v);
        if (!synchronized) return VehiclePhysicsStatus::Rejected;
        if (synchronized.value() == VehiclePhysicsStatus::Pending) return VehiclePhysicsStatus::Pending;
        auto mo     = v.motion();
        mo->x       = b->getX();
        mo->y       = b->getY();
        mo->heading = normalizeDeg(b->getAngle() * 180.f / kPi);
        mo->speed =
            std::cos(b->getAngle()) * b->getLinearVelocityX() + std::sin(b->getAngle()) * b->getLinearVelocityY();
        auto duration = Duration::fromSeconds(double(dt));
        if (!duration) return VehiclePhysicsStatus::Rejected;
        auto nextTick = b->getWorld()->simulationTick().incremented();
        if (!nextTick) return VehiclePhysicsStatus::Rejected;
        auto submitted = submitWheelStep(v, {*nextTick, duration.value()});
        return submitted ? submitted.value() : VehiclePhysicsStatus::Rejected;
    }
    if (eve::physics::Body3D* b = binding ? binding->resolve3D() : nullptr) {
        if (const auto* definition = v.definition()->def; definition && definition->mobility == "ship") {
            shipMove3D(v, b, dt);
            return VehiclePhysicsStatus::Applied;
        }
        auto synchronized = synchronizeWheelState(v);
        if (!synchronized) return VehiclePhysicsStatus::Rejected;
        if (synchronized.value() == VehiclePhysicsStatus::Pending) return VehiclePhysicsStatus::Pending;
        const float qx = b->getRotX(), qy = b->getRotY(), qz = b->getRotZ(), qw = b->getRotW();
        const float fx = 2 * (qx * qz + qw * qy), fy = 2 * (qy * qz - qw * qx), fz = 1 - 2 * (qx * qx + qy * qy);
        auto        mo = v.motion();
        mo->x          = b->getX();
        mo->y          = b->getZ();
        mo->heading    = normalizeDeg(std::atan2(fx, fz) * 180.f / kPi);
        mo->speed      = fx * b->getLinearVelocityX() + fy * b->getLinearVelocityY() + fz * b->getLinearVelocityZ();
        auto duration  = Duration::fromSeconds(double(dt));
        if (!duration) return VehiclePhysicsStatus::Rejected;
        auto nextTick = b->getWorld()->simulationTick().incremented();
        if (!nextTick) return VehiclePhysicsStatus::Rejected;
        auto submitted = submitWheelStep(v, {*nextTick, duration.value()});
        return submitted ? submitted.value() : VehiclePhysicsStatus::Rejected;
    }
    return VehiclePhysicsStatus::Unavailable;
}

Result<VehiclePhysicsStatus> VehiclePhysics::submitWheelStep(VehicleEntity& v, const SimulationStep& step) {
    const auto binding = v.physicsBody()->binding;
    if (!binding || !binding->isAttached())
        return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Unavailable);
    if (auto* body = binding->resolve2D()) return tire_detail::submit2D(v, *body, binding->wheelRuntime, step);
    if (auto* body = binding->resolve3D()) return tire_detail::submit3D(v, *body, binding->wheelRuntime, step);
    return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Unavailable);
}
Result<VehiclePhysicsStatus> VehiclePhysics::synchronizeWheelState(VehicleEntity& v) {
    const auto binding = v.physicsBody()->binding;
    if (!binding) return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Unavailable);
    if (!binding->isAttached()) {
        binding->wheelRuntime.reset();
        return Result<VehiclePhysicsStatus>::success(VehiclePhysicsStatus::Cancelled);
    }
    return tire_detail::synchronize(v, binding->wheelRuntime);
}

void VehiclePhysics::syncTrackFromBody(VehicleEntity& v) {
    auto mo = v.motion();
    const auto binding = v.physicsBody()->binding;
    if (eve::physics::Body* b = binding ? binding->resolve2D() : nullptr) {
        mo->x = b->getX();
        mo->y = b->getY();
    } else if (eve::physics::Body3D* b = binding ? binding->resolve3D() : nullptr) {
        mo->x = b->getX();
        mo->y = b->getZ();
    }
}

VehiclePhysicsStatus VehiclePhysics::tryTrackApply(VehicleEntity& v, float headingRad, float speed) {
    const auto binding = v.physicsBody()->binding;
    if (eve::physics::Body* b = binding ? binding->resolve2D() : nullptr) {
        b->setAngle(headingRad);
        b->setLinearVelocity(std::cos(headingRad) * speed, std::sin(headingRad) * speed);
        return VehiclePhysicsStatus::Applied;
    }
    if (eve::physics::Body3D* b = binding ? binding->resolve3D() : nullptr) {
        b->setRotation(0.f, std::sin(headingRad * 0.5f), 0.f, std::cos(headingRad * 0.5f));
        b->setLinearVelocity(std::sin(headingRad) * speed, 0.f, std::cos(headingRad) * speed);
        return VehiclePhysicsStatus::Applied;
    }
    return VehiclePhysicsStatus::Unavailable;
}

void VehiclePhysics::registerBuiltinMobility() {
    static SuspensionMobility3D mobility;
    VehicleSystem::registerMobility(&mobility);
}

}  // namespace eve::vehicle
