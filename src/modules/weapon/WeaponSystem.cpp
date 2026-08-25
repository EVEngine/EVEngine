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
    void        fire(WeaponEntity&, const AttackRequest&) override {}
    void        update(WeaponEntity&, float) override {}
};

/** @brief 弹道逻辑：有 IProjectileService 时生成投射物，否则只推事件。 */
class ProjectileLogic : public IWeaponLogic {
public:
    const char* name() const override { return "projectile"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity& w, const AttackRequest& req) override {
        if (IProjectileService* svc = eve::cap::query<IProjectileService>()) svc->spawnProjectile(w, req);
    }
    void update(WeaponEntity&, float) override {}
};

/** @brief 近战逻辑：命中数据由事件承载（arc/aoe），逻辑本身无需额外副作用。 */
class MeleeLogic : public IWeaponLogic {
public:
    const char* name() const override { return "melee"; }
    bool        canFire(const WeaponEntity&) const override { return true; }
    void        fire(WeaponEntity&, const AttackRequest&) override {}
    void        update(WeaponEntity&, float) override {}
};

HitscanLogic    gHitscan;
ProjectileLogic gProjectile;
MeleeLogic      gMelee;

const WeaponDefinition* defOf(WeaponEntity& w) { return w.definition()->def; }

/** @brief 是否走阶段机（非热武器形态）。热武器保留成熟弹药/装填/连发管线以保持回归一致。 */
bool usesStageMachine(const WeaponDefinition* def) {
    return def != nullptr && def->kind != WeaponKind::Ranged;
}

void pushFireEvent(WeaponEntity& w, const WeaponDefinition* def, const AttackRequest& req, int ammo) {
    WeaponEvent e;
    e.type        = WeaponEventType::Fire;
    e.weaponId    = w.identity()->id;
    e.defId       = def->id;
    e.ammoLeft    = ammo;
    e.x           = req.muzzleX;
    e.y           = req.muzzleY;
    e.z           = req.muzzleZ;
    e.arc         = req.arcAngle;
    e.aoe         = req.aoeRadius;
    e.spread      = req.spread;
    e.pellets     = std::max(1, req.pelletCount);
    e.recoilPitch = def->recoilPitch;
    e.recoilYaw   = def->recoilYaw;
    e.damageType  = def->damageType;
    e.element     = def->element;
    pushEvent(e);
}

/** @brief 散布 bloom / 后坐回正：随每帧时间推进。 */
void updateFeel(WeaponEntity& w, const WeaponDefinition* def, float dt) {
    auto state = w.state();
    // 散布回稳：从当前连射散布衰减回 spreadMin。
    if (def->spreadRecover > 0.f && state->currentSpread > def->spreadMin) {
        state->currentSpread = std::max(def->spreadMin, state->currentSpread - def->spreadRecover * dt);
    }
    // 后坐回正：向 0 收敛。
    if (def->recoilRecover > 0.f) {
        const float p = std::min(std::fabs(state->recoilPitch), def->recoilRecover * dt);
        state->recoilPitch += state->recoilPitch > 0.f ? -p : p;
        const float y = std::min(std::fabs(state->recoilYaw), def->recoilRecover * dt);
        state->recoilYaw += state->recoilYaw > 0.f ? -y : y;
    }
}

/** @brief 应用一次触发的散布 bloom，返回本发实际散布（度）。 */
float applyBloom(WeaponEntity& w, const WeaponDefinition* def) {
    auto state = w.state();
    if (def->spreadPerShot > 0.f && def->spreadMax > def->spreadMin) {
        state->currentSpread = std::min(def->spreadMax, state->currentSpread + def->spreadPerShot);
    }
    return state->currentSpread;
}

/** @brief 按模板弹丸数逐粒调用 logic->fire，返回总粒数。 */
int spawnPellets(WeaponEntity& w, const WeaponDefinition* def, AttackRequest& r) {
    const int n = std::max(1, def->projectile.pelletCount);
    r.pelletCount = n;
    if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) {
        for (int i = 0; i < n; ++i) {
            r.pelletIndex = i;
            logic->fire(w, r);
        }
    }
    return n;
}

void pushEmptyEvent(WeaponEntity& w, const WeaponDefinition* def) {
    WeaponEvent e;
    e.type     = WeaponEventType::Empty;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = 0;
    pushEvent(e);
}

void pushReloadEnd(WeaponEntity& w, const WeaponDefinition* def, int ammo) {
    WeaponEvent e;
    e.type     = WeaponEventType::ReloadEnd;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = ammo;
    pushEvent(e);
}

// ---------------------------------------------------------------------------
// 热武器（ranged）：成熟管线
// ---------------------------------------------------------------------------

bool canFireRanged(WeaponEntity& w, const WeaponDefinition* def) {
    const auto state = w.state();
    if (state->resource.reloading || state->jammed) return false;
    if (state->cooldown > 0.f) return false;
    if (state->resource.value <= 0.f) return false;
    const IWeaponLogic* logic = WeaponSystem::findLogic(def->logic);
    return logic == nullptr || logic->canFire(w);
}

bool tryFireRanged(WeaponEntity& w, const AttackRequest& req, const WeaponDefinition* def) {
    if (!canFireRanged(w, def)) return false;

    auto state = w.state();
    state->resource.value -= 1.f;

    const float effSpread = applyBloom(w, def);
    state->recoilPitch += def->recoilPitch;
    state->recoilYaw   += def->recoilYaw;

    AttackRequest r = req;
    r.yaw           = w.aim()->yaw;
    r.pitch         = w.aim()->pitch;
    r.spread        = effSpread;
    const int pellets = spawnPellets(w, def, r);

    state->cooldown = def->cooldown;
    if (state->selector == FireMode::Burst && def->burstSize > 1) {
        state->burstRemaining = def->burstSize - 1;
        state->burstTimer     = def->burstInterval;
    }

    r.pelletCount = pellets;
    pushFireEvent(w, def, r, static_cast<int>(state->resource.value));
    if (state->resource.value <= 0.f) pushEmptyEvent(w, def);
    return true;
}

void updateRanged(WeaponEntity& w, const WeaponDefinition* def, float dt) {
    auto     state = w.state();
    Resource& r     = state->resource;
    state->cooldown = std::max(0.f, state->cooldown - dt);
    updateFeel(w, def, dt);

    // 自动装填：弹匣打空、冷却结束且有备弹时自动开始（事件照发，游戏侧可覆盖/取消）。
    if (!r.reloading && r.value <= 0.f && state->cooldown <= 0.f && !state->jammed) {
        WeaponSystem::startReload(w);
    }

    // 装填推进
    if (r.reloading) {
        r.reloadProgress += dt;
        if (r.reloadProgress >= def->reloadTime) {
            const int missing = static_cast<int>(r.max) - static_cast<int>(r.value);
            if (def->reserveSize < 0) {
                r.value = r.max;
            } else if (AmmoPoolEntity* pool = state->ammoPool) {
                const int take = std::min(missing, pool->state()->count);
                r.value += static_cast<float>(take);
                pool->state()->count -= take;
            } else {
                const int take = std::min(missing, r.reserve);
                r.value += static_cast<float>(take);
                r.reserve -= take;
            }
            r.reloading      = false;
            r.reloadProgress = 0.f;
            pushReloadEnd(w, def, static_cast<int>(r.value));
        }
    }

    // 连发续射（第一次扣弹在 tryFire，续发在这里）
    if (!r.reloading && state->burstRemaining > 0) {
        state->burstTimer -= dt;
        if (state->burstTimer <= 0.f) {
            if (r.value > 0.f) {
                r.value -= 1.f;
                AttackRequest req;
                req.yaw     = w.aim()->yaw;
                req.pitch   = w.aim()->pitch;
                req.spread  = applyBloom(w, def);
                state->recoilPitch += def->recoilPitch;
                state->recoilYaw   += def->recoilYaw;
                const int pellets = spawnPellets(w, def, req);
                state->cooldown = def->cooldown;
                state->burstRemaining--;
                state->burstTimer = def->burstInterval;

                req.pelletCount = pellets;
                pushFireEvent(w, def, req, static_cast<int>(r.value));

                if (r.value <= 0.f) {
                    state->burstRemaining = 0;
                    pushEmptyEvent(w, def);
                }
            } else {
                state->burstRemaining = 0;
            }
        }
    }

    if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) logic->update(w, dt);
    WeaponSystem::updateAim(w, dt);
}

// ---------------------------------------------------------------------------
// 阶段机（近战 / 法杖 / 导弹等非热武器形态）
// ---------------------------------------------------------------------------

bool canAttackStage(WeaponEntity& w, const WeaponDefinition* def) {
    const auto state = w.state();
    if (state->stage != AttackStage::Idle) return false;
    if (state->jammed) return false;
    if (state->cooldown > 0.f) return false;
    const Resource& r = state->resource;
    if (!r.infinite && r.value < r.cost) return false;
    const IWeaponLogic* logic = WeaponSystem::findLogic(def->logic);
    return logic == nullptr || logic->canFire(w);
}

bool tryAttackStage(WeaponEntity& w, const AttackRequest& req, const WeaponDefinition* def) {
    if (!canAttackStage(w, def)) return false;

    auto     state = w.state();
    Resource& r     = state->resource;
    if (!r.infinite) r.value = std::max(0.f, r.value - r.cost);

    state->stage       = AttackStage::Windup;
    state->stageTimer  = 0.f;
    state->lastRequest = req;
    if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) logic->begin(w, req);

    WeaponEvent e;
    e.type     = WeaponEventType::WindupStart;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = static_cast<int>(r.value);
    pushEvent(e);
    return true;
}

void updateStage(WeaponEntity& w, const WeaponDefinition* def, float dt) {
    auto state = w.state();
    state->cooldown = std::max(0.f, state->cooldown - dt);
    updateFeel(w, def, dt);

    // 资源回复（法力 / 充能）
    Resource& r = state->resource;
    if (r.regen > 0.f && r.value < r.max) r.value = std::min(r.max, r.value + r.regen * dt);

    const AttackStageSpec* stages = state->stages != nullptr ? state->stages : &def->stages;
    state->stageTimer += dt;

    if (state->stage == AttackStage::Windup && state->stageTimer >= stages->windupTime) {
        // Windup → Active：命中/生效时刻
        state->stage      = AttackStage::Active;
        state->stageTimer = 0.f;
        AttackRequest req = state->lastRequest;
        req.yaw           = w.aim()->yaw;
        req.pitch         = w.aim()->pitch;
        req.spread        = applyBloom(w, def);
        const int pellets = spawnPellets(w, def, req);
        req.pelletCount   = pellets;
        pushFireEvent(w, def, req, static_cast<int>(r.value));
        if (!r.infinite && r.value <= 0.f) pushEmptyEvent(w, def);
    } else if (state->stage == AttackStage::Active) {
        if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) logic->channel(w, state->lastRequest, dt);
        if (state->stageTimer >= stages->activeTime) {
            state->stage      = AttackStage::Recover;
            state->stageTimer = 0.f;
            if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) logic->end(w, state->lastRequest);
            WeaponEvent e;
            e.type     = WeaponEventType::AttackEnd;
            e.weaponId = w.identity()->id;
            e.defId    = def->id;
            e.ammoLeft = static_cast<int>(r.value);
            pushEvent(e);
        }
    } else if (state->stage == AttackStage::Recover && state->stageTimer >= stages->recoverTime) {
        state->stage      = AttackStage::Idle;
        state->stageTimer = 0.f;
    }

    if (IWeaponLogic* logic = WeaponSystem::findLogic(def->logic)) logic->update(w, dt);
    WeaponSystem::updateAim(w, dt);
}

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

void WeaponSystem::emitEvent(const WeaponEvent& e) { pushEvent(e); }

void WeaponSystem::update(WeaponEntity& w, float dt) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return;
    if (usesStageMachine(def)) {
        updateStage(w, def, dt);
    } else {
        updateRanged(w, def, dt);
    }
}

bool WeaponSystem::tryFire(WeaponEntity& w, const AttackRequest& req) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return false;
    if (usesStageMachine(def)) return tryAttackStage(w, req, def);
    return tryFireRanged(w, req, def);
}

bool WeaponSystem::canFire(WeaponEntity& w) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return false;
    if (usesStageMachine(def)) return canAttackStage(w, def);
    return canFireRanged(w, def);
}

void WeaponSystem::startReload(WeaponEntity& w) {
    const WeaponDefinition* def = defOf(w);
    if (def == nullptr) return;
    auto     state = w.state();
    Resource& r     = state->resource;
    if (r.reloading || r.value >= r.max) return;
    if (AmmoPoolEntity* pool = state->ammoPool) {
        if (pool->state()->count <= 0) return;
    } else if (def->reserveSize == 0 || r.reserve <= 0) {
        return;
    }

    r.reloading      = true;
    r.reloadProgress = 0.f;
    state->burstRemaining = 0;

    WeaponEvent e;
    e.type     = WeaponEventType::ReloadStart;
    e.weaponId = w.identity()->id;
    e.defId    = def->id;
    e.ammoLeft = static_cast<int>(r.value);
    pushEvent(e);
}

void WeaponSystem::cancelReload(WeaponEntity& w) {
    w.state()->resource.reloading      = false;
    w.state()->resource.reloadProgress = 0.f;
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
        WeaponSystem::registerLogic(&gMelee);
    }
};

BuiltinLogicRegistrar gBuiltinRegistrar;

}  // namespace
}  // namespace eve::weapon