#include "vehicle/VehicleSystem.h"
#include "vehicle/ContainerAdapters.h"

#include "vehicle/VehicleMobility.h"
#include "vehicle/VehicleOrderQueueAdapter.h"
#include "vehicle/VehiclePhysics.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

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
    const VehicleOrder* current = orders->adapter->current();
    if (current != nullptr) {
        const VehicleOrder& o = *current;
        VehicleEvent        e;
        e.type      = VehicleEventType::OrderCompleted;
        e.vehicleId = v.identity()->id;
        e.defId     = v.identity()->defId;
        e.orderType = vehicleOrderTypeName(o.type);
        e.x         = o.x;
        e.y         = o.y;
        if (orders->adapter->completeCurrent()) {
            pushEvent(e);
            v.motion()->arrived = true;
        }
    }
    orders->adapter->syncCompatibility(*orders);
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
    if (VehiclePhysics::tryWheelMove(v, dt) == VehiclePhysicsStatus::Applied) return;
    kinematicMove(v, dt);
}

/** @brief 履带/坦克移动：差速转向（左右履带速度 = throttle ± steer）。 */
void trackMove(VehicleEntity& v, float dt) {
    const VehicleDefinition* def = v.definition()->def;
    if (def == nullptr) return;
    auto in = v.input();
    auto mo = v.motion();

    VehiclePhysics::syncTrackFromBody(v);

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
    if (VehiclePhysics::tryTrackApply(v, rad, mo->speed) == VehiclePhysicsStatus::Applied) return;
    mo->x += std::cos(rad) * mo->speed * dt;
    mo->y += std::sin(rad) * mo->speed * dt;
}

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

KinematicMobility gKinematic;
WheelMobility     gWheel;
ShipMobility      gShip;
TrackMobility     gTrack;

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
    v.orders()->adapter->update(dt);
    v.orders()->adapter->syncCompatibility(*v.orders());
    processOrders(v, dt);
    const VehicleDefinition* def      = v.definition()->def;
    IVehicleMobility*        mobility = def != nullptr ? findMobility(def->mobility) : nullptr;
    if (mobility != nullptr) mobility->update(v, dt);
}

void VehicleSystem::processOrders(VehicleEntity& v, float dt) {
    auto orders = v.orders();
    const VehicleOrder* current = orders->adapter->current();
    if (current == nullptr) {
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

    const VehicleOrder& o = *current;
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

eve::Result<void> VehicleSystem::pushOrder(VehicleEntity& v, const VehicleOrder& order) {
    auto orders = v.orders();
    const bool replaces = order.type == VehicleOrderType::Move || order.type == VehicleOrderType::AttackMove ||
                          order.type == VehicleOrderType::Stop || order.type == VehicleOrderType::Hold;
    auto queued = replaces ? orders->adapter->replace(order) : orders->adapter->append(order);
    if (!queued.ok()) return eve::Result<void>::failure(queued.status());
    const std::string id = std::move(queued).takeValue();
    if (id.empty()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "vehicle order adapter returned an empty id", "orders"));
    }
    orders->adapter->syncCompatibility(*orders);
    v.motion()->arrived = false;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void VehicleSystem::clearOrders(VehicleEntity& v) {
    auto orders = v.orders();
    orders->adapter->clear();
    orders->adapter->syncCompatibility(*orders);
    v.motion()->arrived = false;
}

const VehicleOrder* VehicleSystem::currentOrder(VehicleEntity& v) { return v.orders()->adapter->current(); }

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
    VehicleSeatContainerAdapter adapter(eve::container::ContainerId("vehicle:seat:" + v.identity()->id), &v);
    auto                        entered = adapter.enter(eve::container::SlotIndex(seatIndex), playerId);
    return entered.ok();
}

bool VehicleSystem::exitSeat(VehicleEntity& v, int seatIndex) {
    VehicleSeatContainerAdapter adapter(eve::container::ContainerId("vehicle:seat:" + v.identity()->id), &v);
    auto                        exited = adapter.exit(eve::container::SlotIndex(seatIndex));
    return exited.ok();
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
        VehiclePhysics::registerBuiltinMobility();
        VehicleSystem::registerDriver(&gPlayerDriver);
    }
};

BuiltinMobilityRegistrar gBuiltinRegistrar;

}  // namespace
}  // namespace eve::vehicle
