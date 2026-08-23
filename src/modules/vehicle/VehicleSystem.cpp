#include "vehicle/VehicleSystem.h"

#include "vehicle/VehicleMobility.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace eve::vehicle {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::unordered_map<std::string, IVehicleMobility*>& mobilityRegistry() {
    static std::unordered_map<std::string, IVehicleMobility*> registry;
    return registry;
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

/** @brief RTS 顶视移动：油门加速 / 转向 / 位置积分（无需物理）。 */
class KinematicMobility : public IVehicleMobility {
public:
    const char* name() const override { return "kinematic"; }

    void update(VehicleEntity& v, float dt) override {
        const VehicleDefinition* def = v.definition()->def;
        if (def == nullptr) return;
        auto in = v.input();
        auto mo = v.motion();

        const float speedFactor = std::clamp(std::fabs(mo->speed) / def->maxSpeed, 0.f, 1.f);
        const float turnPower   = 0.35f + 0.65f * speedFactor;  // 低速可原地转向
        mo->heading += in->steer * def->turnRate * turnPower * dt;
        mo->heading = std::fmod(mo->heading, 360.f);
        if (mo->heading < 0.f) mo->heading += 360.f;

        float target = in->throttle * def->maxSpeed;
        if (in->brake > 0.f || in->handbrake) target = 0.f;
        const float dv    = target - mo->speed;
        const float maxDv = def->accel * dt;
        mo->speed += std::clamp(dv, -maxDv, maxDv);

        const float rad = mo->heading * kPi / 180.f;
        mo->x += std::cos(rad) * mo->speed * dt;
        mo->y += std::sin(rad) * mo->speed * dt;
    }
};

KinematicMobility gKinematic;

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
    auto in      = v.input();
    auto mo      = v.motion();
    in->throttle = 0.f;
    in->steer    = 0.f;
    in->brake    = 0.f;

    auto orders = v.orders();
    if (orders->current < 0 || static_cast<size_t>(orders->current) >= orders->queue.size()) {
        return;
    }

    const VehicleOrder& o = orders->queue[orders->current];
    switch (o.type) {
        case VehicleOrderType::Move:
        case VehicleOrderType::AttackMove: {
            const float dx = o.x - mo->x;
            const float dy = o.y - mo->y;
            if (std::sqrt(dx * dx + dy * dy) <= o.arriveRadius) {
                finishOrder(v);
                return;
            }
            const float desired = std::atan2(dy, dx) * 180.f / kPi;
            in->steer           = steerToward(v, desired);
            in->throttle        = 1.f;
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

}  // namespace eve::vehicle

// 静态注册内置移动模型（链接期执行，模块加载即生效）。
namespace eve::vehicle {
namespace {

struct BuiltinMobilityRegistrar {
    BuiltinMobilityRegistrar() { VehicleSystem::registerMobility(&gKinematic); }
};

BuiltinMobilityRegistrar gBuiltinRegistrar;

}  // namespace
}  // namespace eve::vehicle
