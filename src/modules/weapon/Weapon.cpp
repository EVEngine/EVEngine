#include "weapon/Weapon.h"

#include "common/Json.h"
#include "weapon/WeaponSystem.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>

namespace eve::weapon {

Module_IMPL(Weapon, new Weapon());

namespace {

using eve::json::Value;

bool parseDefinition(Value o, std::unordered_map<std::string, WeaponDefinition>& defs) {
    const std::string id = o.getString("id");
    if (id.empty()) return false;

    WeaponDefinition def;
    def.id          = id;
    def.logic       = o.getString("logic", "projectile");
    def.damage      = o.getFloat("damage");
    def.penetration = o.getFloat("penetration");
    def.range       = o.getFloat("range");
    def.spread      = o.getFloat("spread");
    def.fireMode    = fireModeFromName(o.getString("fireMode", "single"));
    def.cooldown    = o.getFloat("cooldown");

    if (Value burst = o.get("burst")) {
        def.burstSize     = burst.getInt("size", 1);
        def.burstInterval = burst.getFloat("interval");
    }
    if (Value ammo = o.get("ammo")) {
        def.magSize     = ammo.getInt("mag", 1);
        def.reserveSize = ammo.getInt("reserve", 0);
        def.reloadTime  = ammo.getFloat("reload");
    }
    if (Value proj = o.get("projectile")) {
        def.projectile.type    = proj.getString("type", "shell");
        def.projectile.speed   = proj.getFloat("speed");
        def.projectile.gravity = proj.getFloat("gravity");
        def.projectile.aoe     = proj.getFloat("aoe");
    }
    if (Value fx = o.get("effects")) {
        def.effectMuzzle = fx.getString("muzzle");
        def.effectSound  = fx.getString("sound");
    }

    defs[id] = def;
    return true;
}

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

const char* weaponEventTypeName(WeaponEventType type) {
    switch (type) {
        case WeaponEventType::Fire: return "fire";
        case WeaponEventType::ReloadStart: return "reload_start";
        case WeaponEventType::ReloadEnd: return "reload_end";
        case WeaponEventType::Empty: return "empty";
    }
    return "unknown";
}

WeaponEntity* WeaponEntity::createWeapon() {
    WeaponEntity* w = WeaponEntity::create();
    w->identity();
    w->definition();
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

// ---------------------------------------------------------------------------
// 模块生命周期
// ---------------------------------------------------------------------------

Weapon::Weapon() {
    WeaponSystem::setEventSink([this](const WeaponEvent& e) { events_.push_back(e); });
}

Weapon::~Weapon() {
    destroyHandles<WeaponEntity>(weapons_);
    destroyHandles<WeaponMountEntity>(mounts_);
    WeaponSystem::setEventSink(nullptr);
}

// ---------------------------------------------------------------------------
// 武器模板
// ---------------------------------------------------------------------------

int Weapon::registerWeaponsFromJson(const std::string& json) {
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

void Weapon::clearWeaponDefinitions() { defs_.clear(); }

int Weapon::getWeaponDefinitionCount() { return static_cast<int>(defs_.size()); }

bool Weapon::hasWeaponDefinition(const std::string& id) { return defs_.count(id) != 0; }

std::string Weapon::getWeaponDefinitionLogic(const std::string& id) {
    const WeaponDefinition* d = findDef(id);
    return d ? d->logic : std::string{};
}

float Weapon::getWeaponDefinitionDamage(const std::string& id) {
    const WeaponDefinition* d = findDef(id);
    return d ? d->damage : 0.f;
}

float Weapon::getWeaponDefinitionRange(const std::string& id) {
    const WeaponDefinition* d = findDef(id);
    return d ? d->range : 0.f;
}

void Weapon::registerLogic(IWeaponLogic* logic) { WeaponSystem::registerLogic(logic); }

int Weapon::getLogicCount() { return WeaponSystem::logicCount(); }

const WeaponDefinition* Weapon::findDef(const std::string& id) const {
    auto it = defs_.find(id);
    return it == defs_.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------------------

WeaponEntity* Weapon::newWeapon(const std::string& defId) {
    const WeaponDefinition* d = findDef(defId);
    if (d == nullptr) return nullptr;

    WeaponEntity* w         = WeaponEntity::createWeapon();
    w->identity()->id       = defId + "#" + std::to_string(nextInstance_++);
    w->identity()->defId    = defId;
    w->definition()->def    = d;
    w->state()->magAmmo     = d->magSize;
    w->state()->reserveAmmo = d->reserveSize < 0 ? 0 : d->reserveSize;
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
    FireRequest req;
    req.targetX   = x;
    req.targetY   = y;
    req.targetZ   = z;
    req.hasTarget = true;
    req.shooterId = shooterId;
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

// ---------------------------------------------------------------------------
// 脚本绑定
// ---------------------------------------------------------------------------

void Weapon::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Weapon::create, false);
    expose(cls);

    registerCppEntityClassForScript<WeaponEntity>();
    registerCppEntityClassForScript<WeaponMountEntity>();

    auto wCls = table.addClass<WeaponEntity>(
        "WeaponEntity", std::function<WeaponEntity*()>([]() -> WeaponEntity* { return nullptr; }), false);
    wCls.addFunc("getId", [](WeaponEntity* w) -> std::string { return w ? w->identity()->id : std::string{}; });
    wCls.addFunc("getDefId", [](WeaponEntity* w) -> std::string { return w ? w->identity()->defId : std::string{}; });
    wCls.addFunc("getOwnerId", [](WeaponEntity* w) -> int { return w ? w->identity()->ownerId : 0; });
    wCls.addFunc("setOwnerId", [](WeaponEntity* w, int v) {
        if (w) w->identity()->ownerId = v;
    });
    wCls.addFunc("getMagAmmo", [](WeaponEntity* w) -> int { return w ? w->state()->magAmmo : 0; });
    wCls.addFunc("getReserveAmmo", [](WeaponEntity* w) -> int { return w ? w->state()->reserveAmmo : 0; });
    wCls.addFunc("isReloading", [](WeaponEntity* w) -> bool { return w != nullptr && w->state()->reloading; });
    wCls.addFunc("getCooldown", [](WeaponEntity* w) -> float { return w ? w->state()->cooldown : 0.f; });
    wCls.addFunc("getYaw", [](WeaponEntity* w) -> float { return w ? w->aim()->yaw : 0.f; });
    wCls.addFunc("getPitch", [](WeaponEntity* w) -> float { return w ? w->aim()->pitch : 0.f; });
    wCls.addFunc("canFire", [](WeaponEntity* w) -> bool { return w != nullptr && WeaponSystem::canFire(*w); });
    wCls.addFunc("fireAt", [](WeaponEntity* w, float x, float y, float z, int shooterId) -> bool {
        if (w == nullptr) return false;
        FireRequest req;
        req.targetX   = x;
        req.targetY   = y;
        req.targetZ   = z;
        req.hasTarget = true;
        req.shooterId = shooterId;
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
    cls.addFunc("mountAttachWeapon", &Weapon::mountAttachWeapon);
    cls.addFunc("mountGetWeapon", &Weapon::mountGetWeapon);
    cls.addFunc("mountSetLimits", &Weapon::mountSetLimits);
    cls.addFunc("mountAimAt", &Weapon::mountAimAt);
    cls.addFunc("mountDestroy", &Weapon::mountDestroy);
    cls.addFunc("fireAt", &Weapon::fireAt);
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
}

}  // namespace eve::weapon
