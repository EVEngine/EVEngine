#include "weapon/WeaponSystem.h"

#include "common/Capability.h"
#include "weapon/WeaponLogic.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace eve::weapon {

namespace {

std::unordered_map<std::string, IWeaponLogic*>& logicRegistry() {
    static std::unordered_map<std::string, IWeaponLogic*> registry;
    return registry;
}

WeaponEventSink& eventSink() {
    static WeaponEventSink sink;
    return sink;
}

void pushEvent(const WeaponEvent& e) {
    if (eventSink()) eventSink()(e);
}

/** @brief 把角度转到 [-180, 180) 的带符号差。 */
float angleDelta(float from, float to) {
    float d = std::fmod(to - from + 180.f, 360.f);
    if (d < 0.f) d += 360.f;
    return d - 180.f;
}

/** @brief 直射逻辑：无弹道，事件即结果（游戏侧读事件做命中判定/伤害）。 */
class HitscanLogic : public IWeaponLogic {
public:
    const char* name() const override { return "hitscan"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const FireRequest&) override {}
    void        update(WeaponEntity&, float) override {}
};

/** @brief 弹道逻辑：有 IProjectileService 时生成投射物，否则只推事件。 */
class ProjectileLogic : public IWeaponLogic {
public:
    const char* name() const override { return "projectile"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity& w, const FireRequest& req) override {
        if (IProjectileService* svc = eve::cap::query<IProjectileService>()) svc->spawnProjectile(w, req);
    }
    void update(WeaponEntity&, float) override {}
};

HitscanLogic    gHitscan;
ProjectileLogic gProjectile;

const WeaponDefinition* defOf(WeaponEntity& w) { return w.definition()->def; }

}  // namespace

void WeaponSystem::registerLogic(IWeaponLogic* logic) {
    if (logic == nullptr) return;
    logicRegistry()[logic->name()] = logic;
}

IWeaponLogic* WeaponSystem::findLogic(const std::string& name) {
    auto it = logicRegistry().find(name);
    return it == logicRegistry().end() ? nullptr : it->second;
}

int WeaponSystem::logicCount() { return static_cast<int>(logicRegistry().size()); }

void WeaponSystem::setEventSink(WeaponEventSink sink) { eventSink() = std::move(sink); }

void WeaponSystem::update(WeaponEntity& w, float dt) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return;

    auto state      = w.state();
    state->cooldown = std::max(0.f, state->cooldown - dt);

    // 装填推进
    if (state->reloading) {
        state->reloadProgress += dt;
        if (state->reloadProgress >= def->reloadTime) {
            const int missing = def->magSize - state->magAmmo;
            if (def->reserveSize < 0) {
                state->magAmmo = def->magSize;
            } else {
                const int take = std::min(missing, state->reserveAmmo);
                state->magAmmo += take;
                state->reserveAmmo -= take;
            }
            state->reloading      = false;
            state->reloadProgress = 0.f;
            WeaponEvent e;
            e.type     = WeaponEventType::ReloadEnd;
            e.weaponId = w.identity()->id;
            e.defId    = def->id;
            e.ammoLeft = state->magAmmo;
            pushEvent(e);
        }
    }

    // 连发续射（第一次扣弹在 tryFire，续发在这里）
    if (!state->reloading && state->burstRemaining > 0) {
        state->burstTimer -= dt;
        if (state->burstTimer <= 0.f) {
            if (state->magAmmo > 0) {
                state->magAmmo--;
                FireRequest req;
                req.yaw   = w.aim()->yaw;
                req.pitch = w.aim()->pitch;
                if (IWeaponLogic* logic = findLogic(def->logic)) logic->fire(w, req);
                state->cooldown = def->cooldown;
                state->burstRemaining--;
                state->burstTimer = def->burstInterval;

                WeaponEvent e;
                e.type     = WeaponEventType::Fire;
                e.weaponId = w.identity()->id;
                e.defId    = def->id;
                e.ammoLeft = state->magAmmo;
                pushEvent(e);

                if (state->magAmmo == 0) {
                    state->burstRemaining = 0;
                    WeaponEvent empty;
                    empty.type     = WeaponEventType::Empty;
                    empty.weaponId = w.identity()->id;
                    empty.defId    = def->id;
                    empty.ammoLeft = 0;
                    pushEvent(empty);
                }
            } else {
                state->burstRemaining = 0;
            }
        }
    }

    if (IWeaponLogic* logic = findLogic(def->logic)) logic->update(w, dt);
    updateAim(w, dt);
}

bool WeaponSystem::tryFire(WeaponEntity& w, const FireRequest& req) {
    if (!canFire(w)) return false;

    const WeaponDefinition* def   = defOf(w);
    auto                    state = w.state();
    state->magAmmo--;

    FireRequest r = req;
    r.yaw         = w.aim()->yaw;
    r.pitch       = w.aim()->pitch;
    if (IWeaponLogic* logic = findLogic(def->logic)) logic->fire(w, r);

    state->cooldown = def->cooldown;
    if (def->fireMode == FireMode::Burst && def->burstSize > 1) {
        state->burstRemaining = def->burstSize - 1;
        state->burstTimer     = def->burstInterval;
    }

    WeaponEvent e;
    e.type     = WeaponEventType::Fire;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = state->magAmmo;
    e.x        = req.muzzleX;
    e.y        = req.muzzleY;
    e.z        = req.muzzleZ;
    pushEvent(e);

    if (state->magAmmo == 0) {
        WeaponEvent empty;
        empty.type     = WeaponEventType::Empty;
        empty.weaponId = w.identity()->id;
        empty.defId    = def->id;
        empty.ammoLeft = 0;
        pushEvent(empty);
    }
    return true;
}

bool WeaponSystem::canFire(WeaponEntity& w) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return false;
    const auto state = w.state();
    if (state->reloading || state->jammed) return false;
    if (state->cooldown > 0.f) return false;
    if (state->magAmmo <= 0) return false;
    const IWeaponLogic* logic = findLogic(def->logic);
    return logic == nullptr || logic->canFire(w);
}

void WeaponSystem::startReload(WeaponEntity& w) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return;
    auto state = w.state();
    if (state->reloading || state->magAmmo >= def->magSize) return;
    if (def->reserveSize == 0 || state->reserveAmmo <= 0) return;

    state->reloading      = true;
    state->reloadProgress = 0.f;
    state->burstRemaining = 0;

    WeaponEvent e;
    e.type     = WeaponEventType::ReloadStart;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = state->magAmmo;
    pushEvent(e);
}

void WeaponSystem::cancelReload(WeaponEntity& w) {
    w.state()->reloading      = false;
    w.state()->reloadProgress = 0.f;
}

bool WeaponSystem::updateAim(WeaponEntity& w, float dt) {
    auto aim = w.aim();
    if (aim->turnSpeed <= 0.f) {
        aim->yaw   = aim->desiredYaw;
        aim->pitch = aim->desiredPitch;
        return true;
    }
    const float step   = aim->turnSpeed * dt;
    const float dyaw   = angleDelta(aim->yaw, aim->desiredYaw);
    const float dpitch = aim->desiredPitch - aim->pitch;
    aim->yaw += (std::fabs(dyaw) <= step) ? dyaw : (dyaw > 0.f ? step : -step);
    aim->pitch += (std::fabs(dpitch) <= step) ? dpitch : (dpitch > 0.f ? step : -step);
    return std::fabs(angleDelta(aim->yaw, aim->desiredYaw)) < 0.01f &&
           std::fabs(aim->pitch - aim->desiredPitch) < 0.01f;
}

}  // namespace eve::weapon

// 静态注册内置逻辑（链接期执行，模块加载即生效）。
namespace eve::weapon {
namespace {

struct BuiltinLogicRegistrar {
    BuiltinLogicRegistrar() {
        WeaponSystem::registerLogic(&gHitscan);
        WeaponSystem::registerLogic(&gProjectile);
    }
};

BuiltinLogicRegistrar gBuiltinRegistrar;

}  // namespace
}  // namespace eve::weapon
