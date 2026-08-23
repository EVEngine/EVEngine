#include "vehicle/VehicleSystem.h"

#include "vehicle/VehicleMobility.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef EVENGINE_HAS_PHYSICS
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
#endif

namespace eve::vehicle {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::unordered_map<std::string, IVehicleMobility*>& mobilityRegistry() {
    static std::unordered_map<std::string, IVehicleMobility*> registry;
    return registry;
}

std::unordered_map<std::string, IVehicleDriver*>& driverRegistry() {
    static std::unordered_map<std::string, IVehicleDriver*> registry;
    return registry;
}

std::unordered_map<int, PlayerControl>& playerControlsTable() {
    static std::unordered_map<int, PlayerControl> table;
    return table;
}

VehicleEventSink& eventSink() {
    static VehicleEventSink sink;
    return sink;
}

void pushEvent(const VehicleEvent& e) {
    if (eventSink()) eventSink()(e);
}

/** @brief 把角度转到 [-180, 180) 的带符号差。 */
float angleDelta(float from, float to) {
    float d = std::fmod(to - from + 180.f, 360.f);
    if (d < 0.f) d += 360.f;
    return d - 180.f;
}

void finishOrder(VehicleEntity& v) {
    auto orders = v.orders();
    if (orders->current >= 0 && static_cast<size_t>(orders->current) < orders->queue.size()) {
        const VehicleOrder& o = orders->queue[orders->current];
        VehicleEvent        e;
        e.type      = VehicleEventType::OrderCompleted;
        e.vehicleId = v.identity()->id;
        e.defId     = v.identity()->defId;
        e.orderType = vehicleOrderTypeName(o.type);
        e.x         = o.x;
        e.y         = o.y;
        pushEvent(e);
        v.motion()->arrived = true;
        orders->queue.erase(orders->queue.begin() + orders->current);
    }
    orders->current = orders->queue.empty() ? -1 : 0;
}

float normalizeDeg(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

/** @brief 无物理的顶视移动：油门加速 / 转向 / 位置积分。 */
void kinematicMove(VehicleEntity& v, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    auto in = v.input();
    auto mo = v.motion();

    const float speedFactor = std::clamp(std::fabs(mo->speed) / def->maxSpeed, 0.f, 1.f);
    const float turnPower   = 0.35f + 0.65f * speedFactor;  // 低速可原地转向
    mo->heading             = normalizeDeg(mo->heading + in->steer * def->turnRate * turnPower * dt);

    float target = in->throttle * def->maxSpeed;
    if (in->brake > 0.f || in->handbrake) target = 0.f;
    const float dv    = target - mo->speed;
    const float maxDv = def->accel * dt;
    mo->speed += std::clamp(dv, -maxDv, maxDv);

    const float rad = mo->heading * kPi / 180.f;
    mo->x += std::cos(rad) * mo->speed * dt;
    mo->y += std::sin(rad) * mo->speed * dt;
}

/** @brief 轮式移动：2D/3D 物理体优先，无物理回退 kinematic。 */
void wheelMove(VehicleEntity& v, float dt) {
#ifdef EVENGINE_HAS_PHYSICS
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        const VehicleDefinition* def = v.definition()->def;
        if (def == nullptr) return;
        auto in = v.input();
        auto mo = v.motion();
        mo->x   = b->getX();  // 回写碰撞后的位置
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
        return;
    }
    if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
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
        // Y 速度清零 = 悬停语义（无悬架时不会坠落）
        b->setLinearVelocity(std::sin(rad) * mo->speed, 0.f, std::cos(rad) * mo->speed);
        return;
    }
#endif
    kinematicMove(v, dt);
}

/** @brief 履带/坦克移动：差速转向（左右履带速度 = throttle ± steer）。 */
void trackMove(VehicleEntity& v, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    auto in = v.input();
    auto mo = v.motion();

#ifdef EVENGINE_HAS_PHYSICS
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        mo->x = b->getX();
        mo->y = b->getY();
    } else if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
        mo->x = b->getX();
        mo->y = b->getZ();
    }
#endif

    const float left  = std::clamp(in->throttle + in->steer, -1.f, 1.f);
    const float right = std::clamp(in->throttle - in->steer, -1.f, 1.f);
    const float drive = (left + right) * 0.5f;
    const float rot   = (right - left) * 0.5f;

    float target = drive * def->maxSpeed;
    if (in->brake > 0.f || in->handbrake) target = 0.f;
    const float dv    = target - mo->speed;
    const float maxDv = def->accel * dt;
    mo->speed += std::clamp(dv, -maxDv, maxDv);
    mo->heading = normalizeDeg(mo->heading + rot * def->turnRate * dt);  // 原地转向

    const float rad = mo->heading * kPi / 180.f;
#ifdef EVENGINE_HAS_PHYSICS
    if (eve::physics::Body* b = v.physicsBody()->body2d) {
        b->setAngle(rad);
        b->setLinearVelocity(std::cos(rad) * mo->speed, std::sin(rad) * mo->speed);
        return;
    }
    if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
        b->setRotation(0.f, std::sin(rad * 0.5f), 0.f, std::cos(rad * 0.5f));
        b->setLinearVelocity(std::sin(rad) * mo->speed, 0.f, std::cos(rad) * mo->speed);
        return;
    }
#endif
    mo->x += std::cos(rad) * mo->speed * dt;
    mo->y += std::sin(rad) * mo->speed * dt;
}

#ifdef EVENGINE_HAS_PHYSICS
/** @brief 3D raycast 悬架：每轮一根向下的射线，弹簧力 + 阻尼 + 驱动 + 侧向抓地。 */
void suspensionMove3D(VehicleEntity& v, eve::physics::Body3D* b, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    eve::physics::World3D* world = b->getWorld();
    if (world == nullptr) {
        kinematicMove(v, dt);
        return;
    }

    auto in = v.input();
    auto mo = v.motion();
    mo->x   = b->getX();
    mo->y   = b->getZ();

    // 从四元数提取偏航（纯绕 Y 旋转假设）
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
    const uint64_t chassisMask = ~uint64_t{2};  // 排除车体类别位，避免射到自己的底盘

    for (size_t i = 0; i < wheels.size(); ++i) {
        const SuspensionWheel& w  = wheels[i];
        const float            wx = mo->x + w.x * std::cos(yawRad) + w.z * std::sin(yawRad);
        const float            wz = mo->y - w.x * std::sin(yawRad) + w.z * std::cos(yawRad);
        // 射线从悬架顶端（车体挂点 = 轮轴 + 静止长度）向下发出
        const float mountY = b->getY() + w.y + w.restLength;
        const float rayLen = w.restLength + w.radius + def->suspension.maxTravel;
        const int   hit    = world->rayCastFiltered(wx, mountY, wz, wx, mountY - rayLen, wz, chassisMask);

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

    // 驱动与转向
    const float drive       = (in->brake > 0.f || in->handbrake) ? 0.f : in->throttle;
    const float targetSpeed = drive * def->maxSpeed;
    const float fwd =
        std::fabs(targetSpeed) > 0.01f ? (b->getLinearVelocityX() * fx + b->getLinearVelocityZ() * fz) : 0.f;
    float driveForce = def->suspension.driveForce * drive;
    if (std::fabs(targetSpeed) > 0.01f) {
        driveForce *= std::clamp(1.f - fwd / targetSpeed, 0.f, 1.f);  // 限速
    }
    b->applyForce(fx * driveForce, 0.f, fz * driveForce);
    b->setAngularVelocity(0.f, in->steer * def->turnRate * kPi / 180.f, 0.f);

    // 侧向抓地：衰减横向速度分量
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
#endif

/** @brief RTS 顶视移动（默认）：油门加速 / 转向 / 位置积分。 */
class KinematicMobility : public IVehicleMobility {
public:
    const char* name() const override { return "kinematic"; }
    void        update(VehicleEntity& v, float dt) override { kinematicMove(v, dt); }
};

/** @brief 轮式移动：有物理体时走物理，否则回退 kinematic。 */
class WheelMobility : public IVehicleMobility {
public:
    const char* name() const override { return "wheel"; }
    void        update(VehicleEntity& v, float dt) override { wheelMove(v, dt); }
};

/** @brief 船/水面移动：与轮式同构（油门/转向），语义上留给水面游戏。 */
class ShipMobility : public IVehicleMobility {
public:
    const char* name() const override { return "ship"; }
    void        update(VehicleEntity& v, float dt) override { wheelMove(v, dt); }
};

/** @brief 履带/坦克移动：差速转向。 */
class TrackMobility : public IVehicleMobility {
public:
    const char* name() const override { return "track"; }
    void        update(VehicleEntity& v, float dt) override { trackMove(v, dt); }
};

#ifdef EVENGINE_HAS_PHYSICS
/** @brief 3D raycast 悬架移动（Battlefield 式车辆）。 */
class SuspensionMobility3D : public IVehicleMobility {
public:
    const char* name() const override { return "suspension"; }
    void        update(VehicleEntity& v, float dt) override {
        if (eve::physics::Body3D* b = v.physicsBody()->body3d) {
            suspensionMove3D(v, b, dt);
            return;
        }
        kinematicMove(v, dt);
    }
};
#endif

KinematicMobility gKinematic;
WheelMobility     gWheel;
ShipMobility      gShip;
TrackMobility     gTrack;
#ifdef EVENGINE_HAS_PHYSICS
SuspensionMobility3D gSuspension;
#endif

/** @brief 内置玩家驾驶者：从控制表读取玩家输入（VehicleSystem::setPlayerControls）。 */
class PlayerDriver : public IVehicleDriver {
public:
    const char* name() const override { return "player"; }
    bool        sample(VehicleEntity&, int occupantId, VehicleInput& out) override {
        const PlayerControl* pc = VehicleSystem::playerControls(occupantId);
        if (pc == nullptr) return false;
        out.throttle  = pc->throttle;
        out.steer     = pc->steer;
        out.brake     = pc->brake;
        out.handbrake = pc->handbrake;
        out.fire      = pc->fire;
        out.aimYaw    = pc->aimYaw;
        out.aimPitch  = pc->aimPitch;
        return true;
    }
};

PlayerDriver gPlayerDriver;

}  // namespace

void VehicleSystem::registerMobility(IVehicleMobility* mobility) {
    if (mobility == nullptr) return;
    mobilityRegistry()[mobility->name()] = mobility;
}

IVehicleMobility* VehicleSystem::findMobility(const std::string& name) {
    auto it = mobilityRegistry().find(name);
    return it == mobilityRegistry().end() ? nullptr : it->second;
}

int VehicleSystem::mobilityCount() { return static_cast<int>(mobilityRegistry().size()); }

void VehicleSystem::setEventSink(VehicleEventSink sink) { eventSink() = std::move(sink); }

void VehicleSystem::update(VehicleEntity& v, float dt) {
    processOrders(v, dt);
    const VehicleDefinition* def      = v.definition()->def;
    IVehicleMobility*        mobility = def != nullptr ? findMobility(def->mobility) : nullptr;
    if (mobility != nullptr) mobility->update(v, dt);
}

void VehicleSystem::processOrders(VehicleEntity& v, float dt) {
    auto orders = v.orders();
    if (orders->current < 0 || static_cast<size_t>(orders->current) >= orders->queue.size()) {
        return;  // 无命令时保留手动/座位输入
    }

    auto in      = v.input();
    auto mo      = v.motion();
    in->throttle = 0.f;
    in->steer    = 0.f;
    in->brake    = 0.f;
    in->fire     = false;
    in->aimYaw   = 0.f;
    in->aimPitch = 0.f;

    const VehicleOrder& o = orders->queue[orders->current];
    switch (o.type) {
        case VehicleOrderType::Move:
        case VehicleOrderType::AttackMove: {
            const float dx = o.x - mo->x;
            const float dy = o.y - mo->y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= o.arriveRadius) {
                finishOrder(v);
                return;
            }
            const float desired = std::atan2(dy, dx) * 180.f / kPi;
            in->steer = steerToward(v, desired);
            // 大角度转向时减速（缩小转弯半径），接近目标时进一步减速，
            // 否则转弯半径大于 arriveRadius 会导致载具绕着目标永远到不了。
            float throttle = 1.f - 0.75f * std::fabs(in->steer);
            if (dist < 2.5f * o.arriveRadius) {
                throttle *= dist / (2.5f * o.arriveRadius);
            }
            in->throttle = std::clamp(throttle, 0.05f, 1.f);
            break;
        }
        case VehicleOrderType::Attack: {
            const float dx      = o.x - mo->x;
            const float dy      = o.y - mo->y;
            const float desired = std::atan2(dy, dx) * 180.f / kPi;
            in->steer           = steerToward(v, desired);
            in->throttle        = 0.f;  // 原地转向瞄准，不移动
            break;
        }
        case VehicleOrderType::Stop:
        case VehicleOrderType::Hold: in->brake = 1.f; break;
    }
}

void VehicleSystem::pushOrder(VehicleEntity& v, const VehicleOrder& order) {
    auto orders = v.orders();
    if (order.type == VehicleOrderType::Move || order.type == VehicleOrderType::AttackMove ||
        order.type == VehicleOrderType::Stop || order.type == VehicleOrderType::Hold) {
        orders->queue.clear();
        orders->current = -1;
    }
    orders->queue.push_back(order);
    if (orders->current < 0) orders->current = 0;
    v.motion()->arrived = false;
}

void VehicleSystem::clearOrders(VehicleEntity& v) {
    v.orders()->queue.clear();
    v.orders()->current = -1;
    v.motion()->arrived = false;
}

const VehicleOrder* VehicleSystem::currentOrder(VehicleEntity& v) {
    auto orders = v.orders();
    if (orders->current < 0 || static_cast<size_t>(orders->current) >= orders->queue.size()) {
        return nullptr;
    }
    return &orders->queue[orders->current];
}

float VehicleSystem::steerToward(VehicleEntity& v, float targetHeadingDeg) {
    const float delta = angleDelta(v.motion()->heading, targetHeadingDeg);
    return std::clamp(delta / 90.f, -1.f, 1.f);
}

void VehicleSystem::registerDriver(IVehicleDriver* driver) {
    if (driver == nullptr) return;
    driverRegistry()[driver->name()] = driver;
}

IVehicleDriver* VehicleSystem::findDriver(const std::string& name) {
    auto it = driverRegistry().find(name);
    return it == driverRegistry().end() ? nullptr : it->second;
}

int VehicleSystem::driverCount() { return static_cast<int>(driverRegistry().size()); }

void VehicleSystem::setPlayerControls(int playerId, const PlayerControl& control) {
    playerControlsTable()[playerId] = control;
}

const PlayerControl* VehicleSystem::playerControls(int playerId) {
    auto it = playerControlsTable().find(playerId);
    return it == playerControlsTable().end() ? nullptr : &it->second;
}

bool VehicleSystem::enterSeat(VehicleEntity& v, int seatIndex, int playerId) {
    auto seats = v.seats();
    if (seatIndex < 0 || static_cast<size_t>(seatIndex) >= seats->list.size()) return false;
    VehicleEntity::SeatSlot& s = seats->list[static_cast<size_t>(seatIndex)];
    if (s.occupied) return false;
    // 同一玩家不能同时占两个座位
    for (VehicleEntity::SeatSlot& other : seats->list) {
        if (other.occupied && other.occupant == playerId) return false;
    }
    s.occupant = playerId;
    s.occupied = true;
    return true;
}

bool VehicleSystem::exitSeat(VehicleEntity& v, int seatIndex) {
    auto seats = v.seats();
    if (seatIndex < 0 || static_cast<size_t>(seatIndex) >= seats->list.size()) return false;
    VehicleEntity::SeatSlot& s = seats->list[static_cast<size_t>(seatIndex)];
    s.occupied                 = false;
    s.occupant                 = 0;
    return true;
}

int VehicleSystem::findSeatByPlayer(VehicleEntity& v, int playerId) {
    auto seats = v.seats();
    for (size_t i = 0; i < seats->list.size(); ++i) {
        if (seats->list[i].occupied && seats->list[i].occupant == playerId) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace eve::vehicle

// 静态注册内置移动模型（链接期执行，模块加载即生效）。
namespace eve::vehicle {
namespace {

struct BuiltinMobilityRegistrar {
    BuiltinMobilityRegistrar() {
        VehicleSystem::registerMobility(&gKinematic);
        VehicleSystem::registerMobility(&gWheel);
        VehicleSystem::registerMobility(&gShip);
        VehicleSystem::registerMobility(&gTrack);
#ifdef EVENGINE_HAS_PHYSICS
        VehicleSystem::registerMobility(&gSuspension);
#endif
        VehicleSystem::registerDriver(&gPlayerDriver);
    }
};

BuiltinMobilityRegistrar gBuiltinRegistrar;

}  // namespace
}  // namespace eve::vehicle
