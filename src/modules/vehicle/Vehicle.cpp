#include "vehicle/Vehicle.h"

#include "common/Json.h"
#include "vehicle/VehicleSystem.h"
#include "weapon/Weapon.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::vehicle {

Module_IMPL(Vehicle, new Vehicle());

namespace {

using eve::json::Value;

constexpr float kPi = 3.14159265358979323846f;

bool parseDefinition(Value o, std::unordered_map<std::string, VehicleDefinition>& defs) {
    const std::string id = o.getString("id");
    if (id.empty()) return false;

    VehicleDefinition def;
    def.id        = id;
    def.category  = o.getString("category", "vehicle");
    def.mobility  = o.getString("mobility", "kinematic");
    def.maxHealth = o.getFloat("maxHealth", 100.f);

    const Value ph = o.get("physics");
    def.maxSpeed   = ph ? ph.getFloat("maxSpeed", o.getFloat("maxSpeed", 120.f)) : o.getFloat("maxSpeed", 120.f);
    def.accel      = ph ? ph.getFloat("accel", o.getFloat("accel", 80.f)) : o.getFloat("accel", 80.f);
    def.turnRate   = ph ? ph.getFloat("turnRate", o.getFloat("turnRate", 90.f)) : o.getFloat("turnRate", 90.f);
    def.radius     = ph ? ph.getFloat("radius", o.getFloat("radius", 16.f)) : o.getFloat("radius", 16.f);

    if (Value zones = o.get("armorZones")) {
        for (size_t i = 0; i < zones.size(); ++i) {
            Value     z = zones.at(i);
            ArmorZone zone;
            zone.name = z.getString("name", "body");
            zone.mult = z.getFloat("mult", 1.f);
            zone.node = z.getString("node");
            def.armorZones.push_back(zone);
        }
    }

    if (Value mounts = o.get("mounts")) {
        for (size_t i = 0; i < mounts.size(); ++i) {
            Value    m = mounts.at(i);
            MountDef md;
            md.name                         = m.getString("name", "mount" + std::to_string(i));
            md.weapon                       = m.getString("weapon");
            md.type                         = m.getString("type", "turret");
            const std::vector<float> limits = m.getFloatArray("limits");
            if (limits.size() >= 4) {
                md.yawMin   = limits[0];
                md.yawMax   = limits[1];
                md.pitchMin = limits[2];
                md.pitchMax = limits[3];
            }
            md.rotSpeed  = m.getFloat("rotSpeed");
            md.firingArc = m.getFloat("firingArc");
            md.aimMode   = m.getString("aimMode", "auto");
            def.mounts.push_back(md);
        }
    }

    def.tags = o.getStringArray("tags");
    defs[id] = def;
    return true;
}

template <typename T>
T* resolve(const ecs::EntityHandle& h) {
    return static_cast<T*>(ecs::try_get(h));
}

void destroyHandles(std::vector<ecs::EntityHandle>& hs) {
    for (auto& h : hs) {
        if (ecs::Entity* e = ecs::try_get(h)) ecs::DestroyEntity(e);
    }
    hs.clear();
}

template <typename T>
void registerCppEntityClassForScript() {
    eve::registerCppEntityView(typeid(T*).hash_code(), [](ssq::Array& out) {
        HSQUIRRELVM vm = out.getHandle();
        sq_pushobject(vm, out.getRaw());
        ecs::Table* table = ecs::current();
        if (table == nullptr) {
            sq_pop(vm, 1);
            return;
        }
        ecs::IComponentManager&             cm  = table->getOrCreateManager<T>();
        auto*                               reg = cm.getOrCreateRegistryComponentBuffer<T>();
        std::vector<ecs::IComponentBuffer*> stack;
        stack.push_back(reg);
        while (!stack.empty()) {
            ecs::IComponentBuffer* buf = stack.back();
            stack.pop_back();
            auto* r = dynamic_cast<ecs::IRegistryComponentBuffer*>(buf);
            if (r != nullptr) {
                for (uint32_t i = 0; i < r->entity_count(); ++i) {
                    ecs::Entity* ent = r->entity_at(i);
                    if (ent != nullptr && ecs::is_entity_visible(ent)) {
                        ssq::detail::pushByPtr<T>(vm, static_cast<T*>(ent));
                        sq_arrayappend(vm, -2);
                    }
                }
            }
            if (buf->children != nullptr) stack.push_back(buf->children);
            if (buf->next != nullptr) stack.push_back(buf->next);
        }
        sq_pop(vm, 1);
    });
}

float normalizeHeading(float deg) {
    deg = std::fmod(deg, 360.f);
    if (deg < 0.f) deg += 360.f;
    return deg;
}

}  // namespace

const char* vehicleOrderTypeName(VehicleOrderType type) {
    switch (type) {
        case VehicleOrderType::Move: return "move";
        case VehicleOrderType::AttackMove: return "attack_move";
        case VehicleOrderType::Attack: return "attack";
        case VehicleOrderType::Stop: return "stop";
        case VehicleOrderType::Hold: return "hold";
    }
    return "none";
}

const char* vehicleEventTypeName(VehicleEventType type) {
    switch (type) {
        case VehicleEventType::OrderCompleted: return "order_completed";
        case VehicleEventType::Damaged: return "damaged";
        case VehicleEventType::Destroyed: return "destroyed";
    }
    return "unknown";
}

VehicleEntity* VehicleEntity::createVehicle() {
    VehicleEntity* v = VehicleEntity::create();
    v->identity();
    v->definition();
    v->input();
    v->motion();
    v->health();
    v->orders();
    v->mounts();
    return v;
}

// ---------------------------------------------------------------------------
// 模块生命周期
// ---------------------------------------------------------------------------

Vehicle::Vehicle() {
    VehicleSystem::setEventSink([this](const VehicleEvent& e) { events_.push_back(e); });
}

Vehicle::~Vehicle() {
    destroyHandles(vehicles_);
    VehicleSystem::setEventSink(nullptr);
}

// ---------------------------------------------------------------------------
// 载具模板
// ---------------------------------------------------------------------------

int Vehicle::registerVehiclesFromJson(const std::string& json) {
    const eve::json::Document doc = eve::json::Document::parse(json);
    if (!doc.valid()) return 0;
    const Value root = doc.root();
    int         n    = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i)
            if (parseDefinition(root.at(i), defs_)) ++n;
    } else if (root.isObject()) {
        if (parseDefinition(root, defs_)) ++n;
    }
    return n;
}

void Vehicle::clearVehicleDefinitions() { defs_.clear(); }

int Vehicle::getVehicleDefinitionCount() { return static_cast<int>(defs_.size()); }

bool Vehicle::hasVehicleDefinition(const std::string& id) { return defs_.count(id) != 0; }

std::string Vehicle::getVehicleDefinitionMobility(const std::string& id) {
    const VehicleDefinition* d = findDef(id);
    return d ? d->mobility : std::string{};
}

float Vehicle::getVehicleDefinitionMaxHealth(const std::string& id) {
    const VehicleDefinition* d = findDef(id);
    return d ? d->maxHealth : 0.f;
}

void Vehicle::registerMobility(IVehicleMobility* mobility) { VehicleSystem::registerMobility(mobility); }

int Vehicle::getMobilityCount() { return VehicleSystem::mobilityCount(); }

const VehicleDefinition* Vehicle::findDef(const std::string& id) const {
    auto it = defs_.find(id);
    return it == defs_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------------------

VehicleEntity* Vehicle::newVehicle(const std::string& defId, float x, float y, float heading,
                                   const std::string& faction) {
    const VehicleDefinition* def = findDef(defId);
    if (def == nullptr) return nullptr;

    VehicleEntity* v       = VehicleEntity::createVehicle();
    v->identity()->id      = defId + "#" + std::to_string(nextInstance_++);
    v->identity()->defId   = defId;
    v->identity()->faction = faction;
    v->definition()->def   = def;
    v->motion()->x         = x;
    v->motion()->y         = y;
    v->motion()->heading   = normalizeHeading(heading);
    v->health()->maxHp     = def->maxHealth;
    v->health()->hp        = def->maxHealth;

    if (!def->mounts.empty()) {
        if (eve::weapon::Weapon* wmod = requireModInst(eve::weapon, Weapon)) {
            for (const MountDef& md : def->mounts) {
                eve::weapon::WeaponMountEntity* m = wmod->newMount(md.name, md.type);
                if (m == nullptr) continue;
                m->identity()->nodePath = md.name;
                wmod->mountSetLimits(m, md.yawMin, md.yawMax, md.pitchMin, md.pitchMax, md.rotSpeed, md.firingArc);
                if (!md.weapon.empty()) {
                    if (eve::weapon::WeaponEntity* w = wmod->newWeapon(md.weapon)) {
                        wmod->mountAttachWeapon(m, w);
                    }
                }
                VehicleEntity::MountSlot slot;
                slot.mount   = m;
                slot.aimMode = md.aimMode;
                v->mounts()->list.push_back(slot);
            }
        }
    }

    vehicles_.push_back(ecs::handle_of(v));
    return v;
}

// ---------------------------------------------------------------------------
// RTS 命令
// ---------------------------------------------------------------------------

void Vehicle::moveTo(VehicleEntity* v, float x, float y) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::Move;
    o.x    = x;
    o.y    = y;
    VehicleSystem::pushOrder(*v, o);
}

void Vehicle::attackMove(VehicleEntity* v, float x, float y) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::AttackMove;
    o.x    = x;
    o.y    = y;
    VehicleSystem::pushOrder(*v, o);
}

void Vehicle::attack(VehicleEntity* v, float x, float y, int targetId) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type     = VehicleOrderType::Attack;
    o.x        = x;
    o.y        = y;
    o.targetId = targetId;
    VehicleSystem::pushOrder(*v, o);
}

void Vehicle::stop(VehicleEntity* v) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::Stop;
    VehicleSystem::pushOrder(*v, o);
}

void Vehicle::hold(VehicleEntity* v) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::Hold;
    VehicleSystem::pushOrder(*v, o);
}

void Vehicle::clearOrders(VehicleEntity* v) {
    if (v != nullptr) VehicleSystem::clearOrders(*v);
}

int Vehicle::orderCount(VehicleEntity* v) { return v == nullptr ? 0 : static_cast<int>(v->orders()->queue.size()); }

std::string Vehicle::getCurrentOrderType(VehicleEntity* v) {
    if (v == nullptr) return "none";
    const VehicleOrder* o = VehicleSystem::currentOrder(*v);
    return o == nullptr ? "none" : vehicleOrderTypeName(o->type);
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

float Vehicle::getX(VehicleEntity* v) { return v == nullptr ? 0.f : v->motion()->x; }
float Vehicle::getY(VehicleEntity* v) { return v == nullptr ? 0.f : v->motion()->y; }
float Vehicle::getHeading(VehicleEntity* v) { return v == nullptr ? 0.f : v->motion()->heading; }
float Vehicle::getSpeed(VehicleEntity* v) { return v == nullptr ? 0.f : v->motion()->speed; }

void Vehicle::setPosition(VehicleEntity* v, float x, float y) {
    if (v == nullptr) return;
    v->motion()->x = x;
    v->motion()->y = y;
}

void Vehicle::setHeading(VehicleEntity* v, float deg) {
    if (v != nullptr) v->motion()->heading = normalizeHeading(deg);
}

bool Vehicle::isArrived(VehicleEntity* v) { return v != nullptr && v->motion()->arrived; }

float Vehicle::getHealth(VehicleEntity* v) { return v == nullptr ? 0.f : v->health()->hp; }
void  Vehicle::setHealth(VehicleEntity* v, float hp) {
    if (v != nullptr) v->health()->hp = std::max(0.f, hp);
}
float Vehicle::getMaxHealth(VehicleEntity* v) { return v == nullptr ? 0.f : v->health()->maxHp; }

std::string Vehicle::getFaction(VehicleEntity* v) { return v == nullptr ? std::string{} : v->identity()->faction; }

void Vehicle::setFaction(VehicleEntity* v, const std::string& faction) {
    if (v != nullptr) v->identity()->faction = faction;
}

// ---------------------------------------------------------------------------
// 挂点
// ---------------------------------------------------------------------------

int Vehicle::getMountCount(VehicleEntity* v) { return v == nullptr ? 0 : static_cast<int>(v->mounts()->list.size()); }

eve::weapon::WeaponMountEntity* Vehicle::getMount(VehicleEntity* v, int index) {
    if (v == nullptr || index < 0 || static_cast<size_t>(index) >= v->mounts()->list.size()) {
        return nullptr;
    }
    return v->mounts()->list[index].mount;
}

// ---------------------------------------------------------------------------
// 帧推进与自动瞄准
// ---------------------------------------------------------------------------

void Vehicle::update(float dt) {
    for (const ecs::EntityHandle& h : vehicles_) {
        if (VehicleEntity* v = resolve<VehicleEntity>(h)) {
            VehicleSystem::update(*v, dt);
            autoAim(*v);
        }
    }
}

void Vehicle::autoAim(VehicleEntity& v) {
    const VehicleOrder* cur = VehicleSystem::currentOrder(v);
    if (cur == nullptr || (cur->type != VehicleOrderType::Attack && cur->type != VehicleOrderType::AttackMove)) {
        return;
    }
    if (v.mounts()->list.empty()) return;

    eve::weapon::Weapon* wmod = eve::ModuleManager::getInstance<eve::weapon::Weapon>("Weapon");
    if (wmod == nullptr) return;

    auto        mo       = v.motion();
    const float worldYaw = std::atan2(cur->y - mo->y, cur->x - mo->x) * 180.f / kPi;
    float       localYaw = worldYaw - mo->heading;
    localYaw             = std::fmod(localYaw + 180.f, 360.f);
    if (localYaw < 0.f) localYaw += 360.f;
    localYaw -= 180.f;

    for (const VehicleEntity::MountSlot& slot : v.mounts()->list) {
        if (slot.mount == nullptr || slot.mount->state()->destroyed) continue;
        if (slot.aimMode == "auto") wmod->mountAimAt(slot.mount, localYaw, 0.f);
    }
}

// ---------------------------------------------------------------------------
// 事件队列
// ---------------------------------------------------------------------------

void Vehicle::clearEvents() { events_.clear(); }

int Vehicle::getEventCount() const { return static_cast<int>(events_.size()); }

std::string Vehicle::getEventType(int index) const {
    return (index >= 0 && index < getEventCount()) ? vehicleEventTypeName(events_[index].type) : std::string{};
}

std::string Vehicle::getEventVehicleId(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].vehicleId : std::string{};
}

std::string Vehicle::getEventDefId(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].defId : std::string{};
}

std::string Vehicle::getEventOrderType(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].orderType : std::string{};
}

float Vehicle::getEventX(int index) const { return (index >= 0 && index < getEventCount()) ? events_[index].x : 0.f; }

float Vehicle::getEventY(int index) const { return (index >= 0 && index < getEventCount()) ? events_[index].y : 0.f; }

// ---------------------------------------------------------------------------
// 脚本绑定
// ---------------------------------------------------------------------------

void Vehicle::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Vehicle::create, false);
    expose(cls);

    registerCppEntityClassForScript<VehicleEntity>();

    auto vCls = table.addClass<VehicleEntity>(
        "VehicleEntity", std::function<VehicleEntity*()>([]() -> VehicleEntity* { return nullptr; }), false);
    vCls.addFunc("getId", [](VehicleEntity* v) -> std::string { return v ? v->identity()->id : std::string{}; });
    vCls.addFunc("getDefId", [](VehicleEntity* v) -> std::string { return v ? v->identity()->defId : std::string{}; });
    vCls.addFunc("getFaction",
                 [](VehicleEntity* v) -> std::string { return v ? v->identity()->faction : std::string{}; });
    vCls.addFunc("setFaction", [](VehicleEntity* v, const std::string& f) {
        if (v) v->identity()->faction = f;
    });
    vCls.addFunc("getX", [](VehicleEntity* v) -> float { return v ? v->motion()->x : 0.f; });
    vCls.addFunc("getY", [](VehicleEntity* v) -> float { return v ? v->motion()->y : 0.f; });
    vCls.addFunc("getHeading", [](VehicleEntity* v) -> float { return v ? v->motion()->heading : 0.f; });
    vCls.addFunc("getSpeed", [](VehicleEntity* v) -> float { return v ? v->motion()->speed : 0.f; });
    vCls.addFunc("getHealth", [](VehicleEntity* v) -> float { return v ? v->health()->hp : 0.f; });
    vCls.addFunc("getMaxHealth", [](VehicleEntity* v) -> float { return v ? v->health()->maxHp : 0.f; });
    vCls.addFunc("isArrived", [](VehicleEntity* v) -> bool { return v != nullptr && v->motion()->arrived; });
    vCls.addFunc("getCurrentOrderType", [](VehicleEntity* v) -> std::string {
        if (v == nullptr) return "none";
        const VehicleOrder* o = VehicleSystem::currentOrder(*v);
        return o == nullptr ? "none" : vehicleOrderTypeName(o->type);
    });
    vCls.addFunc("getMountCount",
                 [](VehicleEntity* v) -> int { return v == nullptr ? 0 : static_cast<int>(v->mounts()->list.size()); });
    vCls.addFunc("getMount", [](VehicleEntity* v, int index) -> eve::weapon::WeaponMountEntity* {
        if (v == nullptr || index < 0 || static_cast<size_t>(index) >= v->mounts()->list.size()) {
            return nullptr;
        }
        return v->mounts()->list[index].mount;
    });
}

void Vehicle::expose(ssq::Class& cls) {
    cls.addFunc("registerVehiclesFromJson", &Vehicle::registerVehiclesFromJson);
    cls.addFunc("clearVehicleDefinitions", &Vehicle::clearVehicleDefinitions);
    cls.addFunc("getVehicleDefinitionCount", &Vehicle::getVehicleDefinitionCount);
    cls.addFunc("hasVehicleDefinition", &Vehicle::hasVehicleDefinition);
    cls.addFunc("getVehicleDefinitionMobility", &Vehicle::getVehicleDefinitionMobility);
    cls.addFunc("getVehicleDefinitionMaxHealth", &Vehicle::getVehicleDefinitionMaxHealth);
    cls.addFunc("getMobilityCount", [](Vehicle*) -> int { return VehicleSystem::mobilityCount(); });
    cls.addFunc("newVehicle", &Vehicle::newVehicle);
    cls.addFunc("moveTo", &Vehicle::moveTo);
    cls.addFunc("attackMove", &Vehicle::attackMove);
    cls.addFunc("attack", &Vehicle::attack);
    cls.addFunc("stop", &Vehicle::stop);
    cls.addFunc("hold", &Vehicle::hold);
    cls.addFunc("clearOrders", &Vehicle::clearOrders);
    cls.addFunc("orderCount", &Vehicle::orderCount);
    cls.addFunc("getCurrentOrderType", &Vehicle::getCurrentOrderType);
    cls.addFunc("getX", &Vehicle::getX);
    cls.addFunc("getY", &Vehicle::getY);
    cls.addFunc("getHeading", &Vehicle::getHeading);
    cls.addFunc("getSpeed", &Vehicle::getSpeed);
    cls.addFunc("setPosition", &Vehicle::setPosition);
    cls.addFunc("setHeading", &Vehicle::setHeading);
    cls.addFunc("isArrived", &Vehicle::isArrived);
    cls.addFunc("getHealth", &Vehicle::getHealth);
    cls.addFunc("setHealth", &Vehicle::setHealth);
    cls.addFunc("getMaxHealth", &Vehicle::getMaxHealth);
    cls.addFunc("getFaction", &Vehicle::getFaction);
    cls.addFunc("setFaction", &Vehicle::setFaction);
    cls.addFunc("getMountCount", &Vehicle::getMountCount);
    cls.addFunc("getMount", &Vehicle::getMount);
    cls.addFunc("update", &Vehicle::update);
    cls.addFunc("clearEvents", &Vehicle::clearEvents);
    cls.addFunc("getEventCount", &Vehicle::getEventCount);
    cls.addFunc("getEventType", &Vehicle::getEventType);
    cls.addFunc("getEventVehicleId", &Vehicle::getEventVehicleId);
    cls.addFunc("getEventDefId", &Vehicle::getEventDefId);
    cls.addFunc("getEventOrderType", &Vehicle::getEventOrderType);
    cls.addFunc("getEventX", &Vehicle::getEventX);
    cls.addFunc("getEventY", &Vehicle::getEventY);
}

}  // namespace eve::vehicle
