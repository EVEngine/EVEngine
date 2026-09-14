#include "weapon/Weapon.h"

#include "common/Value.h"
#include "weapon/WeaponAttributes.h"
#include "weapon/WeaponDefinitionRuntime.h"
#include "weapon/WeaponSystem.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace eve::weapon {

Module_IMPL(Weapon, new Weapon());

namespace {

template <typename T>
T* resolve(const ecs::EntityHandle& h) {
    return static_cast<T*>(ecs::try_get(h));
}

template <typename T>
void destroyHandles(std::vector<ecs::EntityHandle>& hs) {
    for (auto& h : hs) {
        if (ecs::Entity* e = ecs::try_get(h)) ecs::DestroyEntity(e);
    }
    hs.clear();
}

template <typename T>
void registerCppEntityClassForScript(const ssq::Class& cls) {
    eve::registerCppEntityView(cls, [](ssq::Array& out) {
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

}  // namespace

const char* fireModeName(FireMode mode) {
    switch (mode) {
        case FireMode::Single: return "single";
        case FireMode::Burst: return "burst";
        case FireMode::Auto: return "auto";
    }
    return "single";
}

FireMode fireModeFromName(const std::string& name) {
    if (name == "burst") return FireMode::Burst;
    if (name == "auto") return FireMode::Auto;
    return FireMode::Single;
}

const char* weaponKindName(WeaponKind kind) {
    switch (kind) {
        case WeaponKind::Melee: return "melee";
        case WeaponKind::Ranged: return "ranged";
        case WeaponKind::Magic: return "magic";
        case WeaponKind::Missile: return "missile";
        case WeaponKind::Custom: return "custom";
    }
    return "ranged";
}

WeaponKind weaponKindFromName(const std::string& name) {
    if (name == "melee") return WeaponKind::Melee;
    if (name == "magic") return WeaponKind::Magic;
    if (name == "missile") return WeaponKind::Missile;
    if (name == "custom") return WeaponKind::Custom;
    return WeaponKind::Ranged;
}

const char* attackStageName(AttackStage stage) {
    switch (stage) {
        case AttackStage::Idle: return "idle";
        case AttackStage::Windup: return "windup";
        case AttackStage::Active: return "active";
        case AttackStage::Recover: return "recover";
    }
    return "idle";
}

const char* weaponEventTypeName(WeaponEventType type) {
    switch (type) {
        case WeaponEventType::Fire: return "fire";
        case WeaponEventType::ReloadStart: return "reload_start";
        case WeaponEventType::ReloadEnd: return "reload_end";
        case WeaponEventType::Empty: return "empty";
        case WeaponEventType::WindupStart: return "windup_start";
        case WeaponEventType::AttackEnd: return "attack_end";
        case WeaponEventType::AimIn: return "aim_in";
        case WeaponEventType::AimOut: return "aim_out";
    }
    return "unknown";
}

WeaponEntity* WeaponEntity::createWeapon() {
    WeaponEntity* w = WeaponEntity::create();
    w->identity();
    w->definition();
    w->definitionBinding();
    w->state();
    w->aim();
    return w;
}

WeaponMountEntity* WeaponMountEntity::createMount() {
    WeaponMountEntity* m = WeaponMountEntity::create();
    m->identity();
    m->limits();
    m->state();
    return m;
}

WeaponRigEntity* WeaponRigEntity::createRig() {
    WeaponRigEntity* r = WeaponRigEntity::create();
    r->identity();
    r->held();
    r->state();
    return r;
}

AmmoPoolEntity* AmmoPoolEntity::createPool() {
    AmmoPoolEntity* p = AmmoPoolEntity::create();
    p->identity();
    p->state();
    return p;
}

// ---------------------------------------------------------------------------
// 模块生命周期
// ---------------------------------------------------------------------------

Weapon::Weapon() {
    WeaponSystem::setEventSink([this](const WeaponEvent& e) { events_.push_back(e); });
}

Weapon::~Weapon() {
    destroyHandles<WeaponEntity>(weapons_);
    destroyHandles<WeaponMountEntity>(mounts_);
    destroyHandles<WeaponRigEntity>(rigs_);
    destroyHandles<AmmoPoolEntity>(pools_);
    WeaponSystem::setEventSink(nullptr);
}

// ---------------------------------------------------------------------------
// 武器模板
// ---------------------------------------------------------------------------

int Weapon::registerWeaponsFromJson(const std::string& json) {
    auto canonical = eve::Value::fromJson(json);
    if (!canonical) {
        canonical.ignore("weapon definition input could not be parsed as canonical Value");
        return 0;
    }

    int        n       = 0;
    const auto publish = [this, &n](const eve::Value& value) {
        const auto* object = value.getIf<eve::Value::Object>();
        if (object == nullptr) return;
        const auto  id   = object->find("id");
        const auto* name = id == object->end() ? nullptr : id->second.getIf<std::string>();
        if (name == nullptr || name->empty()) return;

        auto encoded = value.toJson();
        if (!encoded) {
            encoded.ignore("weapon definition value could not be serialized canonically");
            return;
        }
        eve::definitions::Definition source;
        source.type    = "weapon";
        source.id      = *name;
        source.version = eve::SchemaVersion(1);
        source.json    = encoded.value();
        auto typed     = parseWeaponDefinition(source);
        if (!typed) {
            typed.ignore("weapon definition failed typed projection validation");
            return;
        }

        auto       current = definitionRegistry_.resolve("weapon", *name);
        const bool exists  = current.ok();
        if (!exists && current.status().code() != eve::StatusCode::NotFound) return;
        auto stored = exists ? definitionRegistry_.replace("weapon", *name, 1, source.json)
                             : definitionRegistry_.insert("weapon", *name, 1, source.json);
        if (stored) {
            ++n;
        } else {
            stored.ignore("weapon definition registry rejected canonical registration");
        }
    };
    const eve::Value& root = canonical.value();
    if (const auto* array = root.getIf<eve::Value::Array>()) {
        for (const auto& value : *array) publish(value);
    } else {
        publish(root);
    }
    return n;
}

void Weapon::clearWeaponDefinitions() {
    while (definitionRegistry_.countType("weapon") > 0) {
        const auto* definition = definitionRegistry_.atType("weapon", 0);
        if (definition == nullptr) break;
        auto result = definitionRegistry_.remove("weapon", definition->id);
        if (!result) result.ignore("weapon definition registry removal failed");
    }
}

int Weapon::getWeaponDefinitionCount() { return definitionRegistry_.countType("weapon"); }

bool Weapon::hasWeaponDefinition(const std::string& id) { return definitionRegistry_.resolve("weapon", id).ok(); }

std::string Weapon::getWeaponDefinitionLogic(const std::string& id) {
    auto definition = findDef(id);
    if (!definition) {
        definition.ignore("legacy scalar query returns empty for an unknown weapon definition");
        return {};
    }
    return definition.value().logic;
}

float Weapon::getWeaponDefinitionDamage(const std::string& id) {
    auto definition = findDef(id);
    if (!definition) {
        definition.ignore("legacy scalar query returns zero for an unknown weapon definition");
        return 0.f;
    }
    return definition.value().damage;
}

float Weapon::getWeaponDefinitionRange(const std::string& id) {
    auto definition = findDef(id);
    if (!definition) {
        definition.ignore("legacy scalar query returns zero for an unknown weapon definition");
        return 0.f;
    }
    return definition.value().range;
}

void Weapon::registerLogic(IWeaponLogic* logic) { WeaponSystem::registerLogic(logic); }

int Weapon::getLogicCount() { return WeaponSystem::logicCount(); }

eve::Result<WeaponDefinition> Weapon::findDef(const std::string& id) const {
    auto resolved = definitionRegistry_.resolve("weapon", id);
    if (!resolved) return eve::Result<WeaponDefinition>::failure(resolved.status());
    return parseWeaponDefinition(resolved.value().get());
}

// ---------------------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------------------

WeaponEntity* Weapon::newWeapon(const std::string& defId) {
    auto definition = findDef(defId);
    if (!definition) {
        definition.ignore("legacy nullable factory returns null for an unknown weapon definition");
        return nullptr;
    }
    WeaponDefinition d = std::move(definition).takeValue();

    WeaponEntity* w         = WeaponEntity::createWeapon();
    w->identity()->id       = defId + "#" + std::to_string(nextInstance_++);
    w->identity()->defId    = defId;
    w->definition()->owned                   = std::make_shared<const WeaponDefinition>(std::move(d));
    w->definition()->def                     = w->definition()->owned.get();
    w->state()->stages                       = &w->definition()->def->stages;
    const WeaponDefinition& storedDefinition = *w->definition()->def;
    w->state()->selector                     = storedDefinition.fireMode;
    w->state()->currentSpread                = storedDefinition.spreadMin;

    // 运行时资源：热武器 ammo 映射到弹匣/备用；其余形态从模板 resource 拷贝。
    Resource& r = w->state()->resource;
    r.kind      = storedDefinition.resource.kind;
    r.max       = storedDefinition.resource.max;
    r.regen     = storedDefinition.resource.regen;
    r.cost      = storedDefinition.resource.cost;
    r.infinite  = storedDefinition.resource.infinite;
    if (storedDefinition.kind == WeaponKind::Ranged) {
        r.kind     = ResourceKind::Ammo;
        r.max      = static_cast<float>(storedDefinition.magSize);
        r.value    = static_cast<float>(storedDefinition.magSize);
        r.cost     = 1.f;
        r.reserve  = storedDefinition.reserveSize < 0 ? 0 : storedDefinition.reserveSize;
        r.infinite = storedDefinition.reserveSize < 0;
    } else {
        r.value = r.infinite ? 0.f : r.max;
    }

    weapons_.push_back(ecs::handle_of(w));
    return w;
}

WeaponMountEntity* Weapon::newMount(const std::string& id, const std::string& type) {
    WeaponMountEntity* m = WeaponMountEntity::createMount();
    m->identity()->id    = id.empty() ? ("mount#" + std::to_string(nextInstance_++)) : id;
    m->identity()->type  = type;
    mounts_.push_back(ecs::handle_of(m));
    return m;
}

WeaponRigEntity* Weapon::newRig(const std::string& id, const std::string& wield) {
    WeaponRigEntity* r   = WeaponRigEntity::createRig();
    r->identity()->id    = id.empty() ? ("rig#" + std::to_string(nextInstance_++)) : id;
    r->identity()->wield = wield;
    rigs_.push_back(ecs::handle_of(r));
    return r;
}

// ---------------------------------------------------------------------------
// 手持位操作
// ---------------------------------------------------------------------------

bool Weapon::rigAttachWeapon(WeaponRigEntity* rig, WeaponEntity* w) {
    if (rig == nullptr || w == nullptr) return false;
    rig->state()->weapon = w;
    return true;
}

WeaponEntity* Weapon::rigGetWeapon(WeaponRigEntity* rig) { return rig == nullptr ? nullptr : rig->state()->weapon; }

void Weapon::rigSetPose(WeaponRigEntity* rig, float px, float py, float pz, float rx, float ry, float rz) {
    if (rig == nullptr) return;
    rig->held()->posX = px;
    rig->held()->posY = py;
    rig->held()->posZ = pz;
    rig->held()->rotX = rx;
    rig->held()->rotY = ry;
    rig->held()->rotZ = rz;
}

// ---------------------------------------------------------------------------
// 共享弹药池
// ---------------------------------------------------------------------------

AmmoPoolEntity* Weapon::newAmmoPool(const std::string& id, const std::string& ammoType, int max) {
    AmmoPoolEntity* p  = AmmoPoolEntity::createPool();
    p->identity()->id   = id.empty() ? ("ammo#" + std::to_string(nextInstance_++)) : id;
    p->identity()->ammoType = ammoType;
    p->state()->max     = max;
    pools_.push_back(ecs::handle_of(p));
    return p;
}

void Weapon::ammoPoolAdd(AmmoPoolEntity* pool, int n) {
    if (pool == nullptr || n <= 0) return;
    if (pool->state()->max < 0) {
        pool->state()->count += n;
    } else {
        pool->state()->count = std::min(pool->state()->max, pool->state()->count + n);
    }
}

int Weapon::ammoPoolGetCount(AmmoPoolEntity* pool) { return pool == nullptr ? 0 : pool->state()->count; }

bool Weapon::bindAmmoPool(WeaponEntity* w, AmmoPoolEntity* pool) {
    if (w == nullptr || pool == nullptr) return false;
    w->state()->ammoPool = pool;
    return true;
}

void Weapon::unbindAmmoPool(WeaponEntity* w) {
    if (w != nullptr) w->state()->ammoPool = nullptr;
}

AmmoPoolEntity* Weapon::getAmmoPool(WeaponEntity* w) { return w == nullptr ? nullptr : w->state()->ammoPool; }

// ---------------------------------------------------------------------------
// 挂点操作
// ---------------------------------------------------------------------------

bool Weapon::mountAttachWeapon(WeaponMountEntity* m, WeaponEntity* w) {
    if (m == nullptr || w == nullptr || m->state()->destroyed) return false;
    m->state()->weapon = w;
    return true;
}

WeaponEntity* Weapon::mountGetWeapon(WeaponMountEntity* m) { return m == nullptr ? nullptr : m->state()->weapon; }

void Weapon::mountSetLimits(WeaponMountEntity* m, float yawMin, float yawMax, float pitchMin, float pitchMax,
                            float rotSpeed, float firingArc) {
    if (m == nullptr) return;
    m->limits()->yawMin    = yawMin;
    m->limits()->yawMax    = yawMax;
    m->limits()->pitchMin  = pitchMin;
    m->limits()->pitchMax  = pitchMax;
    m->limits()->rotSpeed  = rotSpeed;
    m->limits()->firingArc = firingArc;
}

void Weapon::mountAimAt(WeaponMountEntity* m, float yaw, float pitch) {
    if (m == nullptr || m->state()->destroyed) return;
    const float cyaw   = std::clamp(yaw, m->limits()->yawMin, m->limits()->yawMax);
    const float cpitch = std::clamp(pitch, m->limits()->pitchMin, m->limits()->pitchMax);

    if (WeaponEntity* w = m->state()->weapon) {
        w->aim()->desiredYaw   = cyaw;
        w->aim()->desiredPitch = cpitch;
        w->aim()->turnSpeed    = m->limits()->rotSpeed;
    } else {
        m->state()->yaw   = cyaw;
        m->state()->pitch = cpitch;
    }
}

void Weapon::mountDestroy(WeaponMountEntity* m) {
    if (m == nullptr) return;
    m->state()->destroyed = true;
}

// ---------------------------------------------------------------------------
// 武器操作
// ---------------------------------------------------------------------------

bool Weapon::fire(WeaponEntity* w, const FireRequest& req) { return w != nullptr && WeaponSystem::tryFire(*w, req); }

bool Weapon::fireAt(WeaponEntity* w, float x, float y, float z, int shooterId) {
    if (w == nullptr) return false;
    AttackRequest req;
    req.targetX   = x;
    req.targetY   = y;
    req.targetZ   = z;
    req.hasTarget = true;
    req.shooterId = shooterId;
    return WeaponSystem::tryFire(*w, req);
}

bool Weapon::attack(WeaponEntity* w, float yaw, float pitch, int shooterId) {
    if (w == nullptr) return false;
    AttackRequest req;
    req.yaw       = yaw;
    req.pitch     = pitch;
    req.shooterId = shooterId;
    if (const WeaponDefinition* d = w->definition()->def) {
        req.arcAngle  = d->arc;
        req.aoeRadius = d->projectile.aoe;
    }
    return WeaponSystem::tryFire(*w, req);
}

bool Weapon::canFire(WeaponEntity* w) { return w != nullptr && WeaponSystem::canFire(*w); }

void Weapon::startReload(WeaponEntity* w) {
    if (w != nullptr) WeaponSystem::startReload(*w);
}

void Weapon::cancelReload(WeaponEntity* w) {
    if (w != nullptr) WeaponSystem::cancelReload(*w);
}

void Weapon::setAim(WeaponEntity* w, float yaw, float pitch) {
    if (w == nullptr) return;
    w->aim()->desiredYaw   = yaw;
    w->aim()->desiredPitch = pitch;
}

bool Weapon::setFireMode(WeaponEntity* w, const std::string& mode) {
    if (w == nullptr) return false;
    const WeaponDefinition* d = w->definition()->def;
    if (d == nullptr) return false;
    const FireMode m = fireModeFromName(mode);
    // 无可选模式 = 固定 fireMode，不允许切换。
    if (d->selectableModes.empty()) return m == d->fireMode;
    for (const FireMode sm : d->selectableModes)
        if (sm == m) {
            w->state()->selector = m;
            return true;
        }
    return false;
}

std::string Weapon::getFireMode(WeaponEntity* w) {
    return w != nullptr ? fireModeName(w->state()->selector) : std::string{};
}

int Weapon::getSelectableModeCount(WeaponEntity* w) {
    const WeaponDefinition* d = w != nullptr ? w->definition()->def : nullptr;
    return d != nullptr ? static_cast<int>(d->selectableModes.size()) : 0;
}

std::string Weapon::getSelectableMode(WeaponEntity* w, int index) {
    const WeaponDefinition* d = w != nullptr ? w->definition()->def : nullptr;
    if (d == nullptr || index < 0 || index >= static_cast<int>(d->selectableModes.size())) return std::string{};
    return fireModeName(d->selectableModes[static_cast<size_t>(index)]);
}

bool Weapon::setAiming(WeaponEntity* w, bool aiming) {
    if (w == nullptr || w->state()->aiming == aiming) return false;
    w->state()->aiming = aiming;
    WeaponEvent e;
    e.type     = aiming ? WeaponEventType::AimIn : WeaponEventType::AimOut;
    e.weaponId = w->identity()->id;
    if (const WeaponDefinition* d = w->definition()->def) e.defId = d->id;
    WeaponSystem::emitEvent(e);
    return true;
}

bool Weapon::isAiming(WeaponEntity* w) { return w != nullptr && w->state()->aiming; }

float Weapon::getZoomFov(WeaponEntity* w) {
    const WeaponDefinition* d = w != nullptr ? w->definition()->def : nullptr;
    return d != nullptr ? d->zoomFov : 0.f;
}

float Weapon::getSpread(WeaponEntity* w) { return w != nullptr ? w->state()->currentSpread : 0.f; }

float Weapon::getRecoilPitch(WeaponEntity* w) { return w != nullptr ? w->state()->recoilPitch : 0.f; }

float Weapon::getRecoilYaw(WeaponEntity* w) { return w != nullptr ? w->state()->recoilYaw : 0.f; }

void Weapon::update(float dt) {
    for (const ecs::EntityHandle& h : weapons_) {
        if (WeaponEntity* w = resolve<WeaponEntity>(h)) WeaponSystem::update(*w, dt);
    }
    for (const ecs::EntityHandle& h : mounts_) {
        WeaponMountEntity* m = resolve<WeaponMountEntity>(h);
        if (m == nullptr || m->state()->destroyed) continue;
        if (WeaponEntity* w = m->state()->weapon) {
            m->state()->yaw   = w->aim()->yaw;
            m->state()->pitch = w->aim()->pitch;
        }
    }
}

// ---------------------------------------------------------------------------
// 事件队列
// ---------------------------------------------------------------------------

void Weapon::clearEvents() { events_.clear(); }

int Weapon::getEventCount() const { return static_cast<int>(events_.size()); }

std::string Weapon::getEventType(int index) const {
    return (index >= 0 && index < getEventCount()) ? weaponEventTypeName(events_[index].type) : std::string{};
}

std::string Weapon::getEventWeaponId(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].weaponId : std::string{};
}

std::string Weapon::getEventDefId(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].defId : std::string{};
}

std::string Weapon::getEventMountId(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].mountId : std::string{};
}

int Weapon::getEventAmmoLeft(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].ammoLeft : 0;
}

float Weapon::getEventArc(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].arc : 0.f;
}

float Weapon::getEventAoe(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].aoe : 0.f;
}

float Weapon::getEventSpread(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].spread : 0.f;
}

int Weapon::getEventPellets(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].pellets : 0;
}

float Weapon::getEventRecoilPitch(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].recoilPitch : 0.f;
}

float Weapon::getEventRecoilYaw(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].recoilYaw : 0.f;
}

std::string Weapon::getEventDamageType(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].damageType : std::string{};
}

std::string Weapon::getEventElement(int index) const {
    return (index >= 0 && index < getEventCount()) ? events_[index].element : std::string{};
}

std::string Weapon::getStage(WeaponEntity* w) const {
    return w != nullptr ? attackStageName(w->state()->stage) : std::string{};
}

float Weapon::getResourceValue(WeaponEntity* w) const { return w != nullptr ? w->state()->resource.value : 0.f; }

// ---------------------------------------------------------------------------
// 脚本绑定
// ---------------------------------------------------------------------------

void Weapon::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Weapon::create, false);
    expose(cls);

    auto wCls = table.addClass<WeaponEntity>(
        "WeaponEntity", std::function<WeaponEntity*()>([]() -> WeaponEntity* { return nullptr; }), false);
    registerCppEntityClassForScript<WeaponEntity>(wCls);
    wCls.addFunc("getId", [](WeaponEntity* w) -> std::string { return w ? w->identity()->id : std::string{}; });
    wCls.addFunc("getDefId", [](WeaponEntity* w) -> std::string { return w ? w->identity()->defId : std::string{}; });
    wCls.addFunc("getOwnerId", [](WeaponEntity* w) -> int { return w ? w->identity()->ownerId : 0; });
    wCls.addFunc("setOwnerId", [](WeaponEntity* w, int v) {
        if (w) w->identity()->ownerId = v;
    });
    wCls.addFunc("getMagAmmo", [](WeaponEntity* w) -> int {
        return w ? static_cast<int>(w->state()->resource.value) : 0;
    });
    wCls.addFunc("getReserveAmmo", [](WeaponEntity* w) -> int { return w ? w->state()->resource.reserve : 0; });
    wCls.addFunc("getResourceValue", [](WeaponEntity* w) -> float { return w ? w->state()->resource.value : 0.f; });
    wCls.addFunc("getSpread", [](WeaponEntity* w) -> float { return w ? w->state()->currentSpread : 0.f; });
    wCls.addFunc("getRecoilPitch", [](WeaponEntity* w) -> float { return w ? w->state()->recoilPitch : 0.f; });
    wCls.addFunc("getRecoilYaw", [](WeaponEntity* w) -> float { return w ? w->state()->recoilYaw : 0.f; });
    wCls.addFunc("getFireMode", [](WeaponEntity* w) -> std::string {
        return w ? fireModeName(w->state()->selector) : std::string{};
    });
    wCls.addFunc("setFireMode", [](WeaponEntity* w, const std::string& mode) -> bool {
        if (w == nullptr) return false;
        const WeaponDefinition* d = w->definition()->def;
        if (d == nullptr) return false;
        const FireMode m = fireModeFromName(mode);
        if (d->selectableModes.empty()) return m == d->fireMode;
        for (const FireMode sm : d->selectableModes)
            if (sm == m) {
                w->state()->selector = m;
                return true;
            }
        return false;
    });
    wCls.addFunc("isAiming", [](WeaponEntity* w) -> bool { return w != nullptr && w->state()->aiming; });
    wCls.addFunc("setAiming", [](WeaponEntity* w, bool aiming) -> bool {
        if (w == nullptr || w->state()->aiming == aiming) return false;
        w->state()->aiming = aiming;
        WeaponEvent e;
        e.type     = aiming ? WeaponEventType::AimIn : WeaponEventType::AimOut;
        e.weaponId = w->identity()->id;
        if (const WeaponDefinition* d = w->definition()->def) e.defId = d->id;
        WeaponSystem::emitEvent(e);
        return true;
    });
    wCls.addFunc("getZoomFov", [](WeaponEntity* w) -> float {
        const WeaponDefinition* d = w != nullptr ? w->definition()->def : nullptr;
        return d != nullptr ? d->zoomFov : 0.f;
    });
    wCls.addFunc("getAmmoPool",
                 [](WeaponEntity* w) -> AmmoPoolEntity* { return w != nullptr ? w->state()->ammoPool : nullptr; });
    wCls.addFunc("getStage", [](WeaponEntity* w) -> std::string {
        return w ? attackStageName(w->state()->stage) : std::string{};
    });
    wCls.addFunc("isReloading", [](WeaponEntity* w) -> bool { return w != nullptr && w->state()->resource.reloading; });
    wCls.addFunc("getCooldown", [](WeaponEntity* w) -> float { return w ? w->state()->cooldown : 0.f; });
    wCls.addFunc("getYaw", [](WeaponEntity* w) -> float { return w ? w->aim()->yaw : 0.f; });
    wCls.addFunc("getPitch", [](WeaponEntity* w) -> float { return w ? w->aim()->pitch : 0.f; });
    wCls.addFunc("canFire", [](WeaponEntity* w) -> bool { return w != nullptr && WeaponSystem::canFire(*w); });
    wCls.addFunc("fireAt", [](WeaponEntity* w, float x, float y, float z, int shooterId) -> bool {
        if (w == nullptr) return false;
        AttackRequest req;
        req.targetX   = x;
        req.targetY   = y;
        req.targetZ   = z;
        req.hasTarget = true;
        req.shooterId = shooterId;
        return WeaponSystem::tryFire(*w, req);
    });
    wCls.addFunc("attack", [](WeaponEntity* w, float yaw, float pitch, int shooterId) -> bool {
        if (w == nullptr) return false;
        AttackRequest req;
        req.yaw       = yaw;
        req.pitch     = pitch;
        req.shooterId = shooterId;
        if (const WeaponDefinition* d = w->definition()->def) {
            req.arcAngle  = d->arc;
            req.aoeRadius = d->projectile.aoe;
        }
        return WeaponSystem::tryFire(*w, req);
    });
    wCls.addFunc("startReload", [](WeaponEntity* w) {
        if (w) WeaponSystem::startReload(*w);
    });
    wCls.addFunc("cancelReload", [](WeaponEntity* w) {
        if (w) WeaponSystem::cancelReload(*w);
    });
    wCls.addFunc("setAim", [](WeaponEntity* w, float yaw, float pitch) {
        if (w) {
            w->aim()->desiredYaw   = yaw;
            w->aim()->desiredPitch = pitch;
        }
    });

    auto mCls = table.addClass<WeaponMountEntity>(
        "WeaponMountEntity", std::function<WeaponMountEntity*()>([]() -> WeaponMountEntity* { return nullptr; }),
        false);
    registerCppEntityClassForScript<WeaponMountEntity>(mCls);
    mCls.addFunc("getId", [](WeaponMountEntity* m) -> std::string { return m ? m->identity()->id : std::string{}; });
    mCls.addFunc("getType",
                 [](WeaponMountEntity* m) -> std::string { return m ? m->identity()->type : std::string{}; });
    mCls.addFunc("getNodePath",
                 [](WeaponMountEntity* m) -> std::string { return m ? m->identity()->nodePath : std::string{}; });
    mCls.addFunc("setNodePath", [](WeaponMountEntity* m, const std::string& v) {
        if (m) m->identity()->nodePath = v;
    });
    mCls.addFunc("getWeapon", [](WeaponMountEntity* m) -> WeaponEntity* { return m ? m->state()->weapon : nullptr; });
    mCls.addFunc("getYaw", [](WeaponMountEntity* m) -> float { return m ? m->state()->yaw : 0.f; });
    mCls.addFunc("getPitch", [](WeaponMountEntity* m) -> float { return m ? m->state()->pitch : 0.f; });
    mCls.addFunc("isDestroyed", [](WeaponMountEntity* m) -> bool { return m != nullptr && m->state()->destroyed; });
    mCls.addFunc("destroy", [](WeaponMountEntity* m) {
        if (m) m->state()->destroyed = true;
    });
    mCls.addFunc("aimAt", [](WeaponMountEntity* m, float yaw, float pitch) {
        if (m == nullptr || m->state()->destroyed) return;
        const float cyaw   = std::clamp(yaw, m->limits()->yawMin, m->limits()->yawMax);
        const float cpitch = std::clamp(pitch, m->limits()->pitchMin, m->limits()->pitchMax);
        if (WeaponEntity* w = m->state()->weapon) {
            w->aim()->desiredYaw   = cyaw;
            w->aim()->desiredPitch = cpitch;
            w->aim()->turnSpeed    = m->limits()->rotSpeed;
        } else {
            m->state()->yaw   = cyaw;
            m->state()->pitch = cpitch;
        }
    });

    auto rCls = table.addClass<WeaponRigEntity>(
        "WeaponRigEntity", std::function<WeaponRigEntity*()>([]() -> WeaponRigEntity* { return nullptr; }), false);
    registerCppEntityClassForScript<WeaponRigEntity>(rCls);
    rCls.addFunc("getId", [](WeaponRigEntity* r) -> std::string { return r ? r->identity()->id : std::string{}; });
    rCls.addFunc("getWield",
                 [](WeaponRigEntity* r) -> std::string { return r ? r->identity()->wield : std::string{}; });
    rCls.addFunc("getNodePath",
                 [](WeaponRigEntity* r) -> std::string { return r ? r->identity()->nodePath : std::string{}; });
    rCls.addFunc("setNodePath", [](WeaponRigEntity* r, const std::string& v) {
        if (r) r->identity()->nodePath = v;
    });
    rCls.addFunc("getWeapon", [](WeaponRigEntity* r) -> WeaponEntity* { return r ? r->state()->weapon : nullptr; });
    rCls.addFunc("attachWeapon", [](WeaponRigEntity* r, WeaponEntity* w) -> bool {
        if (r == nullptr || w == nullptr) return false;
        r->state()->weapon = w;
        return true;
    });
    rCls.addFunc("setPose", [](WeaponRigEntity* r, float px, float py, float pz, float rx, float ry, float rz) {
        if (r == nullptr) return;
        r->held()->posX = px;
        r->held()->posY = py;
        r->held()->posZ = pz;
        r->held()->rotX = rx;
        r->held()->rotY = ry;
        r->held()->rotZ = rz;
    });

    auto pCls = table.addClass<AmmoPoolEntity>(
        "AmmoPoolEntity", std::function<AmmoPoolEntity*()>([]() -> AmmoPoolEntity* { return nullptr; }), false);
    registerCppEntityClassForScript<AmmoPoolEntity>(pCls);
    pCls.addFunc("getId", [](AmmoPoolEntity* p) -> std::string { return p ? p->identity()->id : std::string{}; });
    pCls.addFunc("getAmmoType",
                 [](AmmoPoolEntity* p) -> std::string { return p ? p->identity()->ammoType : std::string{}; });
    pCls.addFunc("getCount", [](AmmoPoolEntity* p) -> int { return p ? p->state()->count : 0; });
    pCls.addFunc("addAmmo", [](AmmoPoolEntity* p, int n) {
        if (p == nullptr || n <= 0) return;
        if (p->state()->max < 0) {
            p->state()->count += n;
        } else {
            p->state()->count = std::min(p->state()->max, p->state()->count + n);
        }
    });
    pCls.addFunc("setCount", [](AmmoPoolEntity* p, int n) {
        if (p != nullptr) p->state()->count = n;
    });
}

void Weapon::expose(ssq::Class& cls) {
    cls.addFunc("registerWeaponsFromJson", &Weapon::registerWeaponsFromJson);
    cls.addFunc("clearWeaponDefinitions", &Weapon::clearWeaponDefinitions);
    cls.addFunc("getWeaponDefinitionCount", &Weapon::getWeaponDefinitionCount);
    cls.addFunc("hasWeaponDefinition", &Weapon::hasWeaponDefinition);
    cls.addFunc("getWeaponDefinitionLogic", &Weapon::getWeaponDefinitionLogic);
    cls.addFunc("getWeaponDefinitionDamage", &Weapon::getWeaponDefinitionDamage);
    cls.addFunc("getWeaponDefinitionRange", &Weapon::getWeaponDefinitionRange);
    cls.addFunc("getLogicCount", [](Weapon*) -> int { return WeaponSystem::logicCount(); });
    cls.addFunc("newWeapon", &Weapon::newWeapon);
    cls.addFunc("newMount", &Weapon::newMount);
    cls.addFunc("newRig", &Weapon::newRig);
    cls.addFunc("rigAttachWeapon", &Weapon::rigAttachWeapon);
    cls.addFunc("rigGetWeapon", &Weapon::rigGetWeapon);
    cls.addFunc("rigSetPose", &Weapon::rigSetPose);
    cls.addFunc("newAmmoPool", &Weapon::newAmmoPool);
    cls.addFunc("ammoPoolAdd", &Weapon::ammoPoolAdd);
    cls.addFunc("ammoPoolGetCount", &Weapon::ammoPoolGetCount);
    cls.addFunc("bindAmmoPool", &Weapon::bindAmmoPool);
    cls.addFunc("unbindAmmoPool", &Weapon::unbindAmmoPool);
    cls.addFunc("getAmmoPool", &Weapon::getAmmoPool);
    cls.addFunc("setFireMode", &Weapon::setFireMode);
    cls.addFunc("getFireMode", &Weapon::getFireMode);
    cls.addFunc("getSelectableModeCount", &Weapon::getSelectableModeCount);
    cls.addFunc("getSelectableMode", &Weapon::getSelectableMode);
    cls.addFunc("setAiming", &Weapon::setAiming);
    cls.addFunc("isAiming", &Weapon::isAiming);
    cls.addFunc("getZoomFov", &Weapon::getZoomFov);
    cls.addFunc("getSpread", &Weapon::getSpread);
    cls.addFunc("getRecoilPitch", &Weapon::getRecoilPitch);
    cls.addFunc("getRecoilYaw", &Weapon::getRecoilYaw);
    cls.addFunc("mountAttachWeapon", &Weapon::mountAttachWeapon);
    cls.addFunc("mountGetWeapon", &Weapon::mountGetWeapon);
    cls.addFunc("mountSetLimits", &Weapon::mountSetLimits);
    cls.addFunc("mountAimAt", &Weapon::mountAimAt);
    cls.addFunc("mountDestroy", &Weapon::mountDestroy);
    cls.addFunc("fireAt", &Weapon::fireAt);
    cls.addFunc("attack", &Weapon::attack);
    cls.addFunc("canFire", &Weapon::canFire);
    cls.addFunc("startReload", &Weapon::startReload);
    cls.addFunc("cancelReload", &Weapon::cancelReload);
    cls.addFunc("setAim", &Weapon::setAim);
    cls.addFunc("update", &Weapon::update);
    cls.addFunc("clearEvents", &Weapon::clearEvents);
    cls.addFunc("getEventCount", &Weapon::getEventCount);
    cls.addFunc("getEventType", &Weapon::getEventType);
    cls.addFunc("getEventWeaponId", &Weapon::getEventWeaponId);
    cls.addFunc("getEventDefId", &Weapon::getEventDefId);
    cls.addFunc("getEventMountId", &Weapon::getEventMountId);
    cls.addFunc("getEventAmmoLeft", &Weapon::getEventAmmoLeft);
    cls.addFunc("getEventArc", &Weapon::getEventArc);
    cls.addFunc("getEventAoe", &Weapon::getEventAoe);
    cls.addFunc("getEventSpread", &Weapon::getEventSpread);
    cls.addFunc("getEventPellets", &Weapon::getEventPellets);
    cls.addFunc("getEventRecoilPitch", &Weapon::getEventRecoilPitch);
    cls.addFunc("getEventRecoilYaw", &Weapon::getEventRecoilYaw);
    cls.addFunc("getEventDamageType", &Weapon::getEventDamageType);
    cls.addFunc("getEventElement", &Weapon::getEventElement);
    cls.addFunc("getStage", &Weapon::getStage);
    cls.addFunc("getResourceValue", &Weapon::getResourceValue);
}

}  // namespace eve::weapon
