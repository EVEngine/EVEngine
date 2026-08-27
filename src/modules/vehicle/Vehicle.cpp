#include "vehicle/Vehicle.h"

#include "common/Capability.h"
#include "common/Value.h"
#include "vehicle/VehicleDefinitionRuntime.h"
#include "vehicle/VehicleOrderQueueAdapter.h"
#include "vehicle/VehicleSystem.h"
#include "weapon/Weapon.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

#ifdef EVENGINE_HAS_PHYSICS
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#endif

namespace eve::vehicle {

Module_IMPL(Vehicle, new Vehicle());

namespace {

constexpr float kPi = 3.14159265358979323846f;

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
    v->orders()->adapter->syncCompatibility(*v->orders());
    v->mounts();
    v->physicsBody();
    v->suspension();
    v->seats();
    v->stateFlags();
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
    auto canonical = eve::Value::fromJson(json);
    if (!canonical) {
        canonical.ignore("vehicle definition input could not be parsed as canonical Value");
        return 0;
    }

    int n = 0;
    const auto publish = [this, &n](const eve::Value& value) {
        const auto* object = value.getIf<eve::Value::Object>();
        if (object == nullptr) return;
        const auto id = object->find("id");
        const auto* name = id == object->end() ? nullptr : id->second.getIf<std::string>();
        if (name == nullptr || name->empty()) return;

        auto encoded = value.toJson();
        if (!encoded) {
            encoded.ignore("vehicle definition value could not be serialized canonically");
            return;
        }
        eve::definitions::Definition source;
        source.type = "vehicle";
        source.id = *name;
        source.version = eve::SchemaVersion(1);
        source.json = encoded.value();
        auto typed = parseVehicleDefinition(source);
        if (!typed) {
            typed.ignore("vehicle definition failed typed projection validation");
            return;
        }

        auto current = definitionRegistry_.resolve("vehicle", *name);
        const bool exists = current.ok();
        if (!exists && current.status().code() != eve::StatusCode::NotFound) return;
        auto stored = exists ? definitionRegistry_.replace("vehicle", *name, 1, source.json)
                             : definitionRegistry_.insert("vehicle", *name, 1, source.json);
        if (stored) {
            ++n;
        } else {
            stored.ignore("vehicle definition registry rejected canonical registration");
        }
    };
    const eve::Value& owningRoot = canonical.value();
    if (const auto* array = owningRoot.getIf<eve::Value::Array>()) {
        for (const auto& value : *array) publish(value);
    } else {
        publish(owningRoot);
    }
    return n;
}

void Vehicle::clearVehicleDefinitions() {
    while (definitionRegistry_.countType("vehicle") > 0) {
        const auto* definition = definitionRegistry_.atType("vehicle", 0);
        if (definition == nullptr) break;
        auto result = definitionRegistry_.remove("vehicle", definition->id);
        if (!result) result.ignore("vehicle definition registry removal failed");
    }
}

int Vehicle::getVehicleDefinitionCount() { return definitionRegistry_.countType("vehicle"); }

bool Vehicle::hasVehicleDefinition(const std::string& id) {
    auto result = definitionRegistry_.resolve("vehicle", id);
    return result.ok();
}

std::string Vehicle::getVehicleDefinitionMobility(const std::string& id) {
    auto definition = findDef(id);
    if (!definition) {
        definition.ignore("legacy scalar query returns an empty projection for an unknown definition");
        return {};
    }
    return definition.value().mobility;
}

float Vehicle::getVehicleDefinitionMaxHealth(const std::string& id) {
    auto definition = findDef(id);
    if (!definition) {
        definition.ignore("legacy scalar query returns zero for an unknown definition");
        return 0.f;
    }
    return definition.value().maxHealth;
}

void Vehicle::registerMobility(IVehicleMobility* mobility) { VehicleSystem::registerMobility(mobility); }

int Vehicle::getMobilityCount() { return VehicleSystem::mobilityCount(); }

eve::Result<VehicleDefinition> Vehicle::findDef(const std::string& id) const {
    auto resolved = definitionRegistry_.resolve("vehicle", id);
    if (!resolved) return eve::Result<VehicleDefinition>::failure(resolved.status());
    return parseVehicleDefinition(resolved.value().get());
}

// ---------------------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------------------

VehicleEntity* Vehicle::newVehicle(const std::string& defId, float x, float y, float heading,
                                   const std::string& faction) {
    auto definition = findDef(defId);
    if (!definition) {
        definition.ignore("legacy nullable factory returns null for an unknown definition");
        return nullptr;
    }
    VehicleDefinition def = std::move(definition).takeValue();

    VehicleEntity* v       = VehicleEntity::createVehicle();
    v->identity()->id      = defId + "#" + std::to_string(nextInstance_++);
    v->identity()->defId   = defId;
    v->identity()->faction = faction;
    v->definition()->owned = std::make_shared<const VehicleDefinition>(def);
    v->definition()->def   = v->definition()->owned.get();
    v->motion()->x         = x;
    v->motion()->y         = y;
    v->motion()->heading   = normalizeHeading(heading);
    v->health()->maxHp     = def.maxHealth;
    v->health()->hp        = def.maxHealth;

    if (!def.mounts.empty()) {
        if (eve::weapon::Weapon* wmod = requireModInst(eve::weapon, Weapon)) {
            for (const MountDef& md : def.mounts) {
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

    for (const SeatDef& sd : def.seats) {
        VehicleEntity::SeatSlot slot;
        slot.name       = sd.name;
        slot.driver     = sd.driver;
        slot.cameraMode = sd.cameraMode;
        slot.mountIndex = sd.mountIndex;
        v->seats()->list.push_back(slot);
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
    auto queued = VehicleSystem::pushOrder(*v, o);
    if (!queued) queued.ignore("Vehicle script facade cannot propagate order Result");
}

void Vehicle::attackMove(VehicleEntity* v, float x, float y) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::AttackMove;
    o.x    = x;
    o.y    = y;
    auto queued = VehicleSystem::pushOrder(*v, o);
    if (!queued) queued.ignore("Vehicle script facade cannot propagate order Result");
}

void Vehicle::attack(VehicleEntity* v, float x, float y, int targetId) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type     = VehicleOrderType::Attack;
    o.x        = x;
    o.y        = y;
    o.targetId = targetId;
    auto queued = VehicleSystem::pushOrder(*v, o);
    if (!queued) queued.ignore("Vehicle script facade cannot propagate order Result");
}

void Vehicle::stop(VehicleEntity* v) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::Stop;
    auto queued = VehicleSystem::pushOrder(*v, o);
    if (!queued) queued.ignore("Vehicle script facade cannot propagate order Result");
}

void Vehicle::hold(VehicleEntity* v) {
    if (v == nullptr) return;
    VehicleOrder o;
    o.type = VehicleOrderType::Hold;
    auto queued = VehicleSystem::pushOrder(*v, o);
    if (!queued) queued.ignore("Vehicle script facade cannot propagate order Result");
}

void Vehicle::clearOrders(VehicleEntity* v) {
    if (v != nullptr) VehicleSystem::clearOrders(*v);
}

int Vehicle::orderCount(VehicleEntity* v) {
    return v == nullptr || v->orders()->adapter == nullptr ? 0 : v->orders()->adapter->activeOrQueuedCount();
}

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
// 伤害管线
// ---------------------------------------------------------------------------

void Vehicle::applyDamage(VehicleEntity* v, float amount, const std::string& zone, int sourceId) {
    if (v == nullptr || amount <= 0.f) return;
    if (v->stateFlags()->destroyed) return;

    // 修饰器（多重监听，游戏侧叠加规则，如科技减伤/暴击）
    eve::cap::forEach<IVehicleDamageModifier>(
        [&](IVehicleDamageModifier* m) { amount = m->modifyDamage(*v, amount, zone, sourceId); });
    if (amount <= 0.f) return;

    // 装甲区倍率
    float mult = 1.f;
    if (!zone.empty()) {
        for (const ArmorZone& z : v->definition()->def->armorZones) {
            if (z.name == zone) {
                mult = z.mult;
                break;
            }
        }
    }

    const float hp  = v->health()->hp - amount * mult;
    v->health()->hp = std::max(0.f, hp);

    VehicleEvent e;
    e.vehicleId = v->identity()->id;
    e.defId     = v->identity()->defId;
    e.x         = v->motion()->x;
    e.y         = v->motion()->y;
    if (hp <= 0.f) {
        v->stateFlags()->destroyed = true;
        e.type                     = VehicleEventType::Destroyed;
    } else {
        e.type = VehicleEventType::Damaged;
    }
    events_.push_back(e);
}

float Vehicle::getArmorZoneMult(VehicleEntity* v, const std::string& zone) {
    if (v == nullptr || v->definition()->def == nullptr || zone.empty()) return 1.f;
    for (const ArmorZone& z : v->definition()->def->armorZones) {
        if (z.name == zone) return z.mult;
    }
    return 1.f;
}

bool Vehicle::isDestroyed(VehicleEntity* v) { return v != nullptr && v->stateFlags()->destroyed; }

// ---------------------------------------------------------------------------
// 座位（FPS 面）
// ---------------------------------------------------------------------------

int Vehicle::getSeatCount(VehicleEntity* v) { return v == nullptr ? 0 : static_cast<int>(v->seats()->list.size()); }

std::string Vehicle::getSeatName(VehicleEntity* v, int seatIndex) {
    if (v == nullptr || seatIndex < 0 || static_cast<size_t>(seatIndex) >= v->seats()->list.size()) {
        return std::string{};
    }
    return v->seats()->list[static_cast<size_t>(seatIndex)].name;
}

std::string Vehicle::getSeatCameraMode(VehicleEntity* v, int seatIndex) {
    if (v == nullptr || seatIndex < 0 || static_cast<size_t>(seatIndex) >= v->seats()->list.size()) {
        return std::string{};
    }
    return v->seats()->list[static_cast<size_t>(seatIndex)].cameraMode;
}

bool Vehicle::isSeatOccupied(VehicleEntity* v, int seatIndex) {
    if (v == nullptr || seatIndex < 0 || static_cast<size_t>(seatIndex) >= v->seats()->list.size()) {
        return false;
    }
    return v->seats()->list[static_cast<size_t>(seatIndex)].occupied;
}

int Vehicle::getSeatOccupant(VehicleEntity* v, int seatIndex) {
    if (v == nullptr || seatIndex < 0 || static_cast<size_t>(seatIndex) >= v->seats()->list.size()) {
        return 0;
    }
    return v->seats()->list[static_cast<size_t>(seatIndex)].occupant;
}

eve::weapon::WeaponMountEntity* Vehicle::getSeatMount(VehicleEntity* v, int seatIndex) {
    if (v == nullptr || seatIndex < 0 || static_cast<size_t>(seatIndex) >= v->seats()->list.size()) {
        return nullptr;
    }
    const VehicleEntity::SeatSlot& s = v->seats()->list[static_cast<size_t>(seatIndex)];
    if (s.mountIndex < 0 || static_cast<size_t>(s.mountIndex) >= v->mounts()->list.size()) {
        return nullptr;
    }
    return v->mounts()->list[static_cast<size_t>(s.mountIndex)].mount;
}

bool Vehicle::enterSeat(VehicleEntity* v, int seatIndex, int playerId) {
    return v != nullptr && VehicleSystem::enterSeat(*v, seatIndex, playerId);
}

bool Vehicle::exitSeat(VehicleEntity* v, int seatIndex) {
    return v != nullptr && VehicleSystem::exitSeat(*v, seatIndex);
}

int Vehicle::exitSeatByPlayer(VehicleEntity* v, int playerId) {
    if (v == nullptr) return -1;
    const int idx = VehicleSystem::findSeatByPlayer(*v, playerId);
    if (idx >= 0) VehicleSystem::exitSeat(*v, idx);
    return idx;
}

void Vehicle::setPlayerControls(int playerId, float throttle, float steer, float brake, bool fire, float aimYaw,
                                float aimPitch) {
    PlayerControl c;
    c.throttle = throttle;
    c.steer    = steer;
    c.brake    = brake;
    c.fire     = fire;
    c.aimYaw   = aimYaw;
    c.aimPitch = aimPitch;
    VehicleSystem::setPlayerControls(playerId, c);
}

// ---------------------------------------------------------------------------
// 驾驶输入与物理绑定
// ---------------------------------------------------------------------------

void Vehicle::setInput(VehicleEntity* v, float throttle, float steer, float brake, bool handbrake) {
    if (v == nullptr) return;
    auto in       = v->input();
    in->throttle  = throttle;
    in->steer     = steer;
    in->brake     = brake;
    in->handbrake = handbrake;
}

bool Vehicle::attachPhysics2D(VehicleEntity* v, eve::physics::World* world) {
#ifdef EVENGINE_HAS_PHYSICS
    if (v == nullptr || world == nullptr) return false;
    detachPhysics(v);
    const VehicleDefinition* def = v->definition()->def;
    if (def == nullptr) return false;
    auto mo = v->motion();

    eve::physics::Body* b = world->newBody("dynamic", mo->x, mo->y);
    if (b == nullptr) return false;
    b->newCircleFixture(def->radius, 1.f, 0.6f, 0.f);
    b->setAngle(mo->heading * kPi / 180.f);
    v->physicsBody()->body2d = b;
    v->physicsBody()->space  = "2d";
    return true;
#else
    (void)v;
    (void)world;
    return false;
#endif
}

bool Vehicle::attachPhysics3D(VehicleEntity* v, eve::physics::World3D* world, float heightY) {
#ifdef EVENGINE_HAS_PHYSICS
    if (v == nullptr || world == nullptr) return false;
    detachPhysics(v);
    const VehicleDefinition* def = v->definition()->def;
    if (def == nullptr) return false;
    auto mo = v->motion();

    eve::physics::Body3D* b = world->newBody("dynamic", mo->x, heightY, mo->y);
    if (b == nullptr) return false;
    eve::physics::Shape3D* shape = b->newBoxShape(def->radius, 0.35f, def->radius, 1.f, 0.8f, 0.f);
    // 类别位 2 = 车体；悬架射线掩码排除该位，避免射到自己的底盘
    if (shape != nullptr) shape->setFilterBits(2, ~uint64_t{0});
    const float rad = mo->heading * kPi / 180.f;
    b->setRotation(0.f, std::sin(rad * 0.5f), 0.f, std::cos(rad * 0.5f));
    b->setAwake(true);

    v->physicsBody()->body3d = b;
    v->physicsBody()->space  = "3d";
    v->suspension()->wheels.assign(def->suspension.wheels.size(), {});
    return true;
#else
    (void)v;
    (void)world;
    (void)heightY;
    return false;
#endif
}

bool Vehicle::detachPhysics(VehicleEntity* v) {
    if (v == nullptr) return false;
#ifdef EVENGINE_HAS_PHYSICS
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
    return had;
#else
    return false;
#endif
}

bool Vehicle::hasPhysics(VehicleEntity* v) {
    if (v == nullptr) return false;
    return v->physicsBody()->body2d != nullptr || v->physicsBody()->body3d != nullptr;
}

std::string Vehicle::getPhysicsSpace(VehicleEntity* v) {
    return v == nullptr ? std::string{} : v->physicsBody()->space;
}

float Vehicle::getHeight(VehicleEntity* v) {
#ifdef EVENGINE_HAS_PHYSICS
    if (v != nullptr && v->physicsBody()->body3d != nullptr) {
        return v->physicsBody()->body3d->getY();
    }
#else
    (void)v;
#endif
    return 0.f;
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
            updateSeats(*v);                // 座位输入写入（无命令时生效）
            VehicleSystem::update(*v, dt);  // 命令优先，然后移动
            autoAim(*v);                    // RTS 攻击命令自动瞄准
        }
    }
}

void Vehicle::updateSeats(VehicleEntity& v) {
    auto seats = v.seats();
    if (seats->list.empty()) return;

    eve::weapon::Weapon* wmod          = eve::ModuleManager::getInstance<eve::weapon::Weapon>("Weapon");
    bool                 driverApplied = false;

    for (const VehicleEntity::SeatSlot& s : seats->list) {
        if (!s.occupied) continue;
        IVehicleDriver* driver = VehicleSystem::findDriver(s.driver);
        if (driver == nullptr) continue;

        VehicleInput in;
        if (!driver->sample(v, s.occupant, in)) continue;

        // 驾驶员座位：写移动输入（只取第一个有效驾驶员）
        if (!driverApplied && s.name == "driver") {
            auto vi       = v.input();
            vi->throttle  = in.throttle;
            vi->steer     = in.steer;
            vi->brake     = in.brake;
            vi->handbrake = in.handbrake;
            vi->fire      = in.fire;
            vi->aimYaw    = in.aimYaw;
            vi->aimPitch  = in.aimPitch;
            driverApplied = true;
        }

        // 武器座位：瞄准 + 开火
        if (s.mountIndex >= 0 && static_cast<size_t>(s.mountIndex) < v.mounts()->list.size() && wmod != nullptr) {
            eve::weapon::WeaponMountEntity* m = v.mounts()->list[s.mountIndex].mount;
            if (m == nullptr || m->state()->destroyed) continue;
            wmod->mountAimAt(m, in.aimYaw, in.aimPitch);
            if (in.fire) {
                if (eve::weapon::WeaponEntity* w = wmod->mountGetWeapon(m)) {
                    eve::weapon::FireRequest req;
                    req.yaw       = in.aimYaw;
                    req.pitch     = in.aimPitch;
                    req.shooterId = s.occupant;
                    wmod->fire(w, req);
                }
            }
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

    const float dist = std::sqrt((cur->x - mo->x) * (cur->x - mo->x) +
                                 (cur->y - mo->y) * (cur->y - mo->y));
    for (const VehicleEntity::MountSlot& slot : v.mounts()->list) {
        if (slot.mount == nullptr || slot.mount->state()->destroyed) continue;
        if (slot.aimMode != "auto") continue;
        wmod->mountAimAt(slot.mount, localYaw, 0.f);
        // 攻击命令：炮塔在射程内自动开火；attackMove 只负责边走边瞄准。
        if (cur->type != VehicleOrderType::Attack) continue;
        eve::weapon::WeaponEntity* w = wmod->mountGetWeapon(slot.mount);
        if (w == nullptr || !wmod->canFire(w)) continue;
        const std::string& defId = w->identity()->defId;
        const float        range = wmod->getWeaponDefinitionRange(defId);
        if (range > 0.f && dist > range) continue;
        wmod->fireAt(w, cur->x, cur->y, 0.f, cur->targetId);
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
    vCls.addFunc("isDestroyed", [](VehicleEntity* v) -> bool { return v != nullptr && v->stateFlags()->destroyed; });
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
    cls.addFunc("setInput", &Vehicle::setInput);
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
    cls.addFunc("applyDamage", &Vehicle::applyDamage);
    cls.addFunc("getArmorZoneMult", &Vehicle::getArmorZoneMult);
    cls.addFunc("isDestroyed", &Vehicle::isDestroyed);
    cls.addFunc("getSeatCount", &Vehicle::getSeatCount);
    cls.addFunc("getSeatName", &Vehicle::getSeatName);
    cls.addFunc("getSeatCameraMode", &Vehicle::getSeatCameraMode);
    cls.addFunc("isSeatOccupied", &Vehicle::isSeatOccupied);
    cls.addFunc("getSeatOccupant", &Vehicle::getSeatOccupant);
    cls.addFunc("getSeatMount", &Vehicle::getSeatMount);
    cls.addFunc("enterSeat", &Vehicle::enterSeat);
    cls.addFunc("exitSeat", &Vehicle::exitSeat);
    cls.addFunc("exitSeatByPlayer", &Vehicle::exitSeatByPlayer);
    cls.addFunc("setPlayerControls", &Vehicle::setPlayerControls);
    cls.addFunc("detachPhysics", &Vehicle::detachPhysics);
    cls.addFunc("hasPhysics", &Vehicle::hasPhysics);
    cls.addFunc("getPhysicsSpace", &Vehicle::getPhysicsSpace);
    cls.addFunc("getHeight", &Vehicle::getHeight);
#ifdef EVENGINE_HAS_PHYSICS
    cls.addFunc("attachPhysics2D", &Vehicle::attachPhysics2D);
    cls.addFunc("attachPhysics3D", &Vehicle::attachPhysics3D);
#endif
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
