#include "vehicle/VehiclePhysics.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eve::vehicle {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float normalizeDeg(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

void wheelMove2D(VehicleEntity& v, eve::physics::Body* b, float dt) {
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

void wheelMove3D(VehicleEntity& v, eve::physics::Body3D* b, float dt) {
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

void suspensionMove3D(VehicleEntity& v, eve::physics::Body3D* b, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    eve::physics::World3D* world = b->getWorld();
    if (world == nullptr) {
        if (IVehicleMobility* kinematic = VehicleSystem::findMobility("kinematic")) kinematic->update(v, dt);
        return;
    }

    auto in = v.input();
    auto mo = v.motion();
    mo->x   = b->getX();
    mo->y   = b->getZ();

    const float qx     = b->getRotX();
    const float qy     = b->getRotY();
    const float qz     = b->getRotZ();
    const float qw     = b->getRotW();
    const float yaw    = std::atan2(2.f * (qw * qy + qx * qz), 1.f - 2.f * (qy * qy + qz * qz));
    mo->heading        = normalizeDeg(yaw * 180.f / kPi);
    const float yawRad = mo->heading * kPi / 180.f;
    const float fx     = std::sin(yawRad);
    const float fz     = std::cos(yawRad);

    const auto& wheels = def->suspension.wheels;
    auto        sus    = v.suspension();
    if (sus->wheels.size() != wheels.size()) sus->wheels.resize(wheels.size());
    const uint64_t chassisMask = ~uint64_t{2};

    for (size_t i = 0; i < wheels.size(); ++i) {
        const SuspensionWheel& w  = wheels[i];
        const float            wx = mo->x + w.x * std::cos(yawRad) + w.z * std::sin(yawRad);
        const float            wz = mo->y - w.x * std::sin(yawRad) + w.z * std::cos(yawRad);
        const float            mountY = b->getY() + w.y + w.restLength;
        const float            rayLen = w.restLength + w.radius + def->suspension.maxTravel;
        const int              hit    = world->rayCastFiltered(wx, mountY, wz, wx, mountY - rayLen, wz, chassisMask);

        float compression = 0.f;
        if (hit >= 0) {
            const float hitDist = mountY - world->getRayHitY();
            compression         = std::clamp(w.restLength - (hitDist - w.radius), 0.f, def->suspension.maxTravel);
        }

        auto&       ws    = sus->wheels[i];
        const float vel   = (compression - ws.prevCompression) / std::max(dt, 1e-4f);
        const float force = w.stiffness * compression + w.damping * vel;
        if (force > 0.f) b->applyForceAt(0.f, force, 0.f, wx, mountY, wz);
        ws.prevCompression = compression;
        ws.grounded        = hit >= 0;
    }

    const float drive       = (in->brake > 0.f || in->handbrake) ? 0.f : in->throttle;
    const float targetSpeed = drive * def->maxSpeed;
    const float fwd =
        std::fabs(targetSpeed) > 0.01f ? (b->getLinearVelocityX() * fx + b->getLinearVelocityZ() * fz) : 0.f;
    float driveForce = def->suspension.driveForce * drive;
    if (std::fabs(targetSpeed) > 0.01f) {
        driveForce *= std::clamp(1.f - fwd / targetSpeed, 0.f, 1.f);
    }
    b->applyForce(fx * driveForce, 0.f, fz * driveForce);
    b->setAngularVelocity(0.f, in->steer * def->turnRate * kPi / 180.f, 0.f);

    const float vx   = b->getLinearVelocityX();
    const float vy   = b->getLinearVelocityY();
    const float vz   = b->getLinearVelocityZ();
    const float rx   = fz;
    const float rz   = -fx;
    const float lat  = vx * rx + vz * rz;
    const float grip = std::max(0.f, 1.f - def->suspension.lateralGrip * dt);
    b->setLinearVelocity(fx * fwd + rx * lat * grip, vy, fz * fwd + rz * lat * grip);
    mo->speed = fwd;
}

class SuspensionMobility3D : public IVehicleMobility {
public:
    const char* name() const override { return "suspension"; }
    void        update(VehicleEntity& v, float dt) override {
        if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
            suspensionMove3D(v, b, dt);
            return;
        }
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
    v->physicsBody()->body2d = b;
    v->physicsBody()->space  = "2d";
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

    v->physicsBody()->body3d = b;
    v->physicsBody()->space  = "3d";
    v->suspension()->wheels.assign(def->suspension.wheels.size(), {});
    return VehiclePhysicsStatus::Applied;
}

VehiclePhysicsStatus VehiclePhysics::detach(VehicleEntity* v) {
    if (v == nullptr) return VehiclePhysicsStatus::Unavailable;
    auto pb  = v->physicsBody();
    bool had = false;
    if (pb->body2d != nullptr) {
        pb->body2d->destroy();
        had = true;
    }
    if (pb->body3d != nullptr) {
        pb->body3d->destroy();
        had = true;
    }
    pb->body2d = nullptr;
    pb->body3d = nullptr;
    pb->space.clear();
    return had ? VehiclePhysicsStatus::Applied : VehiclePhysicsStatus::Unavailable;
}

float VehiclePhysics::height(VehicleEntity* v) {
    if (v != nullptr && v->physicsBody()->body3d != nullptr) return v->physicsBody()->body3d->getY();
    return 0.f;
}

VehiclePhysicsStatus VehiclePhysics::tryWheelMove(VehicleEntity& v, float dt) {
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        wheelMove2D(v, b, dt);
        return VehiclePhysicsStatus::Applied;
    }
    if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
        wheelMove3D(v, b, dt);
        return VehiclePhysicsStatus::Applied;
    }
    return VehiclePhysicsStatus::Unavailable;
}

void VehiclePhysics::syncTrackFromBody(VehicleEntity& v) {
    auto mo = v.motion();
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        mo->x = b->getX();
        mo->y = b->getY();
    } else if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
        mo->x = b->getX();
        mo->y = b->getZ();
    }
}

VehiclePhysicsStatus VehiclePhysics::tryTrackApply(VehicleEntity& v, float headingRad, float speed) {
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        b->setAngle(headingRad);
        b->setLinearVelocity(std::cos(headingRad) * speed, std::sin(headingRad) * speed);
        return VehiclePhysicsStatus::Applied;
    }
    if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
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
