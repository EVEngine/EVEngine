#include "weapon/WeaponDefinitionRuntime.h"

#include "common/Value.h"
#include "weapon/Weapon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace eve::weapon {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                                          std::move(path), {}, "weapon.definition_runtime"));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

bool readText(const eve::Value::Object& object, std::string_view name, std::string& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    const auto* text = value->getIf<std::string>();
    if (text == nullptr) return false;
    output = *text;
    return true;
}

bool readNumber(const eve::Value::Object& object, std::string_view name, float& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    if (const auto* number = value->getIf<double>()) {
        if (!std::isfinite(*number)) return false;
        output = static_cast<float>(*number);
        return std::isfinite(output);
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        output = static_cast<float>(*integer);
        return std::isfinite(output);
    }
    return false;
}

bool readInteger(const eve::Value::Object& object, std::string_view name, int& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    const auto* integer = value->getIf<std::int64_t>();
    if (integer == nullptr || *integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max())
        return false;
    output = static_cast<int>(*integer);
    return true;
}

bool readBoolean(const eve::Value::Object& object, std::string_view name, bool& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    const auto* boolean = value->getIf<bool>();
    if (boolean == nullptr) return false;
    output = *boolean;
    return true;
}

bool readStringArray(const eve::Value::Object& object, std::string_view name,
                     std::vector<std::string>& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    const auto* array = value->getIf<eve::Value::Array>();
    if (array == nullptr) return false;
    std::vector<std::string> candidate;
    candidate.reserve(array->size());
    for (const auto& entry : *array) {
        const auto* text = entry.getIf<std::string>();
        if (text == nullptr || text->empty()) return false;
        candidate.push_back(*text);
    }
    std::sort(candidate.begin(), candidate.end());
    if (std::adjacent_find(candidate.begin(), candidate.end()) != candidate.end()) return false;
    output = std::move(candidate);
    return true;
}

const char* resourceKindName(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::None: return "none";
        case ResourceKind::Ammo: return "ammo";
        case ResourceKind::Mana: return "mana";
        case ResourceKind::Charges: return "charges";
        case ResourceKind::Stamina: return "stamina";
    }
    return "none";
}

ResourceKind resourceKindFromName(std::string_view name) noexcept {
    if (name == "ammo") return ResourceKind::Ammo;
    if (name == "mana") return ResourceKind::Mana;
    if (name == "charges") return ResourceKind::Charges;
    if (name == "stamina") return ResourceKind::Stamina;
    return ResourceKind::None;
}

AttackStage attackStageFromName(std::string_view name) noexcept {
    if (name == "windup") return AttackStage::Windup;
    if (name == "active") return AttackStage::Active;
    if (name == "recover") return AttackStage::Recover;
    return AttackStage::Idle;
}

eve::Result<WeaponDefinition> parseDefinition(const eve::definitions::Definition& source,
                                              const eve::DefinitionRef&           reference) {
    auto parsed = eve::Value::fromJson(source.json);
    if (!parsed) return eve::Result<WeaponDefinition>::failure(parsed.status());
    auto        value  = std::move(parsed).takeValue();
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<WeaponDefinition>("weapon definition must be an object", "json");

    WeaponDefinition result;
    result.id      = std::string(reference.id().name());
    std::string id = result.id;
    if (!readText(*object, "id", id) || id != result.id)
        return invalid<WeaponDefinition>("weapon definition id does not match its logical reference", "id");
    std::string kind  = weaponKindName(result.kind);
    std::string logic = result.logic;
    if (!readText(*object, "kind", kind) || !readText(*object, "logic", logic))
        return invalid<WeaponDefinition>("weapon kind and logic must be strings", "definition");
    result.kind  = weaponKindFromName(kind);
    result.logic = std::move(logic);

    if (!readNumber(*object, "damage", result.damage) || !readNumber(*object, "penetration", result.penetration) ||
        !readNumber(*object, "range", result.range) || !readNumber(*object, "spread", result.spread) ||
        !readNumber(*object, "falloffStart", result.falloffStart) ||
        !readNumber(*object, "minimumDamageFactor", result.minimumDamageFactor) ||
        !readNumber(*object, "splashMinimumDamageFactor", result.splashMinimumDamageFactor) ||
        !readNumber(*object, "preferredTargetBonus", result.preferredTargetBonus) ||
        !readNumber(*object, "accuracy", result.accuracy) ||
        !readNumber(*object, "scatterRadius", result.scatterRadius) ||
        !readNumber(*object, "spreadMin", result.spreadMin) || !readNumber(*object, "spreadMax", result.spreadMax) ||
        !readNumber(*object, "spreadPerShot", result.spreadPerShot) ||
        !readNumber(*object, "spreadRecover", result.spreadRecover) ||
        !readNumber(*object, "recoilPitch", result.recoilPitch) ||
        !readNumber(*object, "recoilYaw", result.recoilYaw) ||
        !readNumber(*object, "recoilRecover", result.recoilRecover) ||
        !readNumber(*object, "zoomFov", result.zoomFov) || !readNumber(*object, "cooldown", result.cooldown) ||
        !readNumber(*object, "arc", result.arc))
        return invalid<WeaponDefinition>("weapon numeric fields must be finite numbers", "definition");
    if (result.falloffStart < 0.0f || result.falloffStart > result.range ||
        result.minimumDamageFactor < 0.0f || result.minimumDamageFactor > 1.0f ||
        result.splashMinimumDamageFactor < 0.0f || result.splashMinimumDamageFactor > 1.0f ||
        result.accuracy < 0.0f || result.accuracy > 1.0f || result.scatterRadius < 0.0f ||
        result.preferredTargetBonus < 1.0f)
        return invalid<WeaponDefinition>("weapon damage falloff fields are outside valid ranges", "definition");

    if (!readText(*object, "damageType", result.damageType) || !readText(*object, "element", result.element) ||
        !readBoolean(*object, "targetsGround", result.targetsGround) ||
        !readBoolean(*object, "targetsAir", result.targetsAir) ||
        !readBoolean(*object, "friendlyFire", result.friendlyFire) ||
        !readBoolean(*object, "blockedByObstacles", result.blockedByObstacles) ||
        !readStringArray(*object, "requiredTargetTags", result.requiredTargetTags) ||
        !readStringArray(*object, "excludedTargetTags", result.excludedTargetTags) ||
        !readStringArray(*object, "preferredTargetTags", result.preferredTargetTags))
        return invalid<WeaponDefinition>("weapon target policy fields are invalid", "definition");
    if (!result.targetsGround && !result.targetsAir)
        return invalid<WeaponDefinition>("weapon must target ground, air, or both", "definition");

    if (const auto* modes = field(*object, "fireModes")) {
        const auto* array = modes->getIf<eve::Value::Array>();
        if (array == nullptr) return invalid<WeaponDefinition>("fireModes must be an array", "fireModes");
        for (const auto& entry : *array) {
            const auto* text = entry.getIf<std::string>();
            if (text == nullptr) return invalid<WeaponDefinition>("fire mode must be a string", "fireModes");
            result.selectableModes.push_back(fireModeFromName(*text));
        }
    }
    std::string fireMode = fireModeName(result.fireMode);
    if (!readText(*object, "fireMode", fireMode))
        return invalid<WeaponDefinition>("fireMode must be a string", "fireMode");
    result.fireMode = fireModeFromName(fireMode);

    if (const auto* resource = field(*object, "resource")) {
        const auto* nested = resource->getIf<eve::Value::Object>();
        if (nested == nullptr) return invalid<WeaponDefinition>("resource must be an object", "resource");
        std::string resourceName = resourceKindName(result.resource.kind);
        if (!readText(*nested, "kind", resourceName) || !readNumber(*nested, "max", result.resource.max) ||
            !readNumber(*nested, "regen", result.resource.regen) ||
            !readNumber(*nested, "cost", result.resource.cost) ||
            !readBoolean(*nested, "infinite", result.resource.infinite))
            return invalid<WeaponDefinition>("weapon resource fields are invalid", "resource");
        result.resource.kind = resourceKindFromName(resourceName);
    }
    if (const auto* stages = field(*object, "stages")) {
        const auto* nested = stages->getIf<eve::Value::Object>();
        if (nested == nullptr || !readNumber(*nested, "windup", result.stages.windupTime) ||
            !readNumber(*nested, "active", result.stages.activeTime) ||
            !readNumber(*nested, "recover", result.stages.recoverTime))
            return invalid<WeaponDefinition>("stages fields are invalid", "stages");
    }
    if (const auto* burst = field(*object, "burst")) {
        const auto* nested = burst->getIf<eve::Value::Object>();
        if (nested == nullptr || !readInteger(*nested, "size", result.burstSize) ||
            !readNumber(*nested, "interval", result.burstInterval))
            return invalid<WeaponDefinition>("burst fields are invalid", "burst");
    }
    if (const auto* ammo = field(*object, "ammo")) {
        const auto* nested = ammo->getIf<eve::Value::Object>();
        if (nested == nullptr || !readInteger(*nested, "mag", result.magSize) ||
            !readInteger(*nested, "reserve", result.reserveSize) || !readNumber(*nested, "reload", result.reloadTime))
            return invalid<WeaponDefinition>("ammo fields are invalid", "ammo");
    }
    if (const auto* projectile = field(*object, "projectile")) {
        const auto* nested = projectile->getIf<eve::Value::Object>();
        if (nested == nullptr || !readText(*nested, "type", result.projectile.type) ||
            !readNumber(*nested, "speed", result.projectile.speed) ||
            !readNumber(*nested, "gravity", result.projectile.gravity) ||
            !readNumber(*nested, "aoe", result.projectile.aoe) ||
            !readInteger(*nested, "pelletCount", result.projectile.pelletCount) ||
            !readNumber(*nested, "pelletSpread", result.projectile.pelletSpread))
            return invalid<WeaponDefinition>("projectile fields are invalid", "projectile");
    }
    if (const auto* effects = field(*object, "effects")) {
        const auto* nested = effects->getIf<eve::Value::Object>();
        if (nested == nullptr || !readText(*nested, "muzzle", result.effectMuzzle) ||
            !readText(*nested, "sound", result.effectSound))
            return invalid<WeaponDefinition>("effects fields are invalid", "effects");
    }
    if (result.resource.kind == ResourceKind::None) result.resource.infinite = true;
    return eve::Result<WeaponDefinition>::success(std::move(result));
}

eve::Result<eve::definition::DefinitionHandle> currentHandle(eve::definitions::DefinitionRegistry& registry,
                                                             const eve::DefinitionRef&             reference) {
    const auto& logical = reference.id();
    return registry.handle(std::string(logical.namespaceName()), std::string(logical.name()));
}

eve::Result<WeaponDefinition> definitionFor(eve::definitions::DefinitionRegistry&    registry,
                                            const eve::definition::DefinitionHandle& handle,
                                            const eve::DefinitionRef&                reference) {
    auto resolved = registry.resolveHandle(handle);
    if (!resolved) return eve::Result<WeaponDefinition>::failure(resolved.status());
    return parseDefinition(resolved.value().get(), reference);
}

eve::LogicalId runtimeSchema() {
    static const eve::LogicalId value = [] {
        auto parsed = eve::LogicalId::parse("weapon:runtime");
        if (!parsed) throw std::logic_error("invalid built-in weapon snapshot schema");
        return *parsed;
    }();
    return value;
}

WeaponRuntimeState initialState(const WeaponDefinition& definition) {
    WeaponRuntimeState result;
    result.selector          = definition.fireMode;
    result.currentSpread     = definition.spreadMin;
    result.resource.kind     = definition.resource.kind;
    result.resource.max      = definition.resource.max;
    result.resource.regen    = definition.resource.regen;
    result.resource.cost     = definition.resource.cost;
    result.resource.infinite = definition.resource.infinite;
    if (definition.kind == WeaponKind::Ranged) {
        result.resource.kind     = ResourceKind::Ammo;
        result.resource.max      = static_cast<float>(definition.magSize);
        result.resource.value    = static_cast<float>(definition.magSize);
        result.resource.cost     = 1.f;
        result.resource.reserve  = definition.reserveSize < 0 ? 0 : definition.reserveSize;
        result.resource.infinite = definition.reserveSize < 0;
    } else {
        result.resource.value = result.resource.infinite ? 0.f : result.resource.max;
    }
    return result;
}

eve::Value encodeState(const WeaponRuntimeState& state) {
    const Resource&    resource = state.resource;
    eve::Value::Object resourceObject{
        {"cost", eve::Value(resource.cost)},
        {"infinite", eve::Value(resource.infinite)},
        {"kind", eve::Value(resourceKindName(resource.kind))},
        {"max", eve::Value(resource.max)},
        {"regen", eve::Value(resource.regen)},
        {"reloadProgress", eve::Value(resource.reloadProgress)},
        {"reloading", eve::Value(resource.reloading)},
        {"reserve", eve::Value(resource.reserve)},
        {"value", eve::Value(resource.value)},
    };
    return eve::Value(eve::Value::Object{
        {"aiming", eve::Value(state.aiming)},
        {"burstRemaining", eve::Value(state.burstRemaining)},
        {"burstTimer", eve::Value(state.burstTimer)},
        {"cooldown", eve::Value(state.cooldown)},
        {"currentSpread", eve::Value(state.currentSpread)},
        {"jammed", eve::Value(state.jammed)},
        {"recoilPitch", eve::Value(state.recoilPitch)},
        {"recoilYaw", eve::Value(state.recoilYaw)},
        {"resource", eve::Value(std::move(resourceObject))},
        {"selector", eve::Value(fireModeName(state.selector))},
        {"stage", eve::Value(attackStageName(state.stage))},
        {"stageTimer", eve::Value(state.stageTimer)},
    });
}

bool readStateNumber(const eve::Value::Object& object, const char* name, float& output) {
    return readNumber(object, name, output);
}

eve::Result<WeaponRuntimeState> decodeState(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<WeaponRuntimeState>("weapon runtime state must be an object", "state");
    static const std::set<std::string> fields = {"aiming",        "burstRemaining", "burstTimer",  "cooldown",
                                                 "currentSpread", "jammed",         "recoilPitch", "recoilYaw",
                                                 "resource",      "selector",       "stage",       "stageTimer"};
    for (const auto& [name, unused] : *object) {
        (void)unused;
        if (!fields.contains(name))
            return invalid<WeaponRuntimeState>("unknown weapon runtime state field", "state." + name);
    }
    for (const auto& name : fields)
        if (!object->contains(name))
            return invalid<WeaponRuntimeState>("weapon runtime state is missing a field", "state." + name);

    WeaponRuntimeState result;
    std::string        selector;
    std::string        stage;
    if (!readBoolean(*object, "aiming", result.aiming) ||
        !readInteger(*object, "burstRemaining", result.burstRemaining) ||
        !readStateNumber(*object, "burstTimer", result.burstTimer) ||
        !readStateNumber(*object, "cooldown", result.cooldown) ||
        !readStateNumber(*object, "currentSpread", result.currentSpread) ||
        !readBoolean(*object, "jammed", result.jammed) ||
        !readStateNumber(*object, "recoilPitch", result.recoilPitch) ||
        !readStateNumber(*object, "recoilYaw", result.recoilYaw) || !readText(*object, "selector", selector) ||
        !readText(*object, "stage", stage) || !readStateNumber(*object, "stageTimer", result.stageTimer))
        return invalid<WeaponRuntimeState>("weapon runtime scalar field is invalid", "state");
    result.selector           = fireModeFromName(selector);
    result.stage              = attackStageFromName(stage);
    const auto* resourceValue = object->at("resource").getIf<eve::Value::Object>();
    if (resourceValue == nullptr)
        return invalid<WeaponRuntimeState>("weapon runtime resource is invalid", "state.resource");
    std::string kind;
    if (!readText(*resourceValue, "kind", kind) || !readStateNumber(*resourceValue, "value", result.resource.value) ||
        !readStateNumber(*resourceValue, "max", result.resource.max) ||
        !readStateNumber(*resourceValue, "regen", result.resource.regen) ||
        !readStateNumber(*resourceValue, "cost", result.resource.cost) ||
        !readBoolean(*resourceValue, "infinite", result.resource.infinite) ||
        !readInteger(*resourceValue, "reserve", result.resource.reserve) ||
        !readBoolean(*resourceValue, "reloading", result.resource.reloading) ||
        !readStateNumber(*resourceValue, "reloadProgress", result.resource.reloadProgress))
        return invalid<WeaponRuntimeState>("weapon runtime resource fields are invalid", "state.resource");
    result.resource.kind = resourceKindFromName(kind);
    return eve::Result<WeaponRuntimeState>::success(std::move(result));
}

}  // namespace

eve::Result<WeaponDefinition> parseWeaponDefinition(const eve::definitions::Definition& source) {
    auto logical = eve::LogicalId::fromParts("weapon", source.id);
    if (!logical) return invalid<WeaponDefinition>("weapon definition id is invalid", "id");
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference) return eve::Result<WeaponDefinition>::failure(reference.status());
    if (source.type != "weapon") return invalid<WeaponDefinition>("definition type must be weapon", "type");
    return parseDefinition(source, reference.value());
}

eve::Result<WeaponDefinitionRuntime> WeaponDefinitionRuntime::create(eve::definitions::DefinitionRegistry& registry,
                                                                     eve::DefinitionRef                    definition,
                                                                     eve::PersistentId                     instanceId,
                                                                     eve::definition::ReloadPolicy         policy) {
    if (!definition.id().isValid() || definition.id().namespaceName() != "weapon")
        return invalid<WeaponDefinitionRuntime>("weapon definition reference must use weapon namespace", "definition");
    auto handle = currentHandle(registry, definition);
    if (!handle) return eve::Result<WeaponDefinitionRuntime>::failure(handle.status());
    auto typed = definitionFor(registry, handle.value(), definition);
    if (!typed) return eve::Result<WeaponDefinitionRuntime>::failure(typed.status());
    auto runtime = eve::definition::RuntimeInstance<WeaponRuntimeState>::create(
        instanceId, std::move(definition), handle.value().generation, initialState(typed.value()));
    if (!runtime) return eve::Result<WeaponDefinitionRuntime>::failure(runtime.status());
    return eve::Result<WeaponDefinitionRuntime>::success(
        WeaponDefinitionRuntime(registry, std::move(runtime).takeValue(), policy));
}

eve::Result<WeaponDefinitionRuntime> WeaponDefinitionRuntime::create(eve::definitions::DefinitionRegistry& registry,
                                                                     std::string_view                      definitionId,
                                                                     eve::PersistentId                     instanceId,
                                                                     eve::definition::ReloadPolicy         policy) {
    auto logical = eve::LogicalId::fromParts("weapon", definitionId);
    if (!logical) return invalid<WeaponDefinitionRuntime>("weapon id is not a valid logical name", "definitionId");
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference) return eve::Result<WeaponDefinitionRuntime>::failure(reference.status());
    return create(registry, std::move(reference).takeValue(), instanceId, policy);
}

eve::Result<WeaponDefinitionRuntime> WeaponDefinitionRuntime::create(Weapon& module, std::string_view definitionId,
                                                                     eve::PersistentId             instanceId,
                                                                     eve::definition::ReloadPolicy policy) {
    return create(module.definitionRegistry(), definitionId, instanceId, policy);
}

const eve::definition::InstanceIdentity& WeaponDefinitionRuntime::identity() const noexcept {
    return runtime_.identity();
}

const WeaponRuntimeState& WeaponDefinitionRuntime::state() const noexcept { return runtime_.state(); }

WeaponRuntimeState& WeaponDefinitionRuntime::state() noexcept { return runtime_.state(); }

eve::definition::DefinitionHandle WeaponDefinitionRuntime::definitionHandle() const noexcept {
    return runtime_.identity().definitionHandle();
}

eve::Result<WeaponDefinition> WeaponDefinitionRuntime::definition() const {
    if (registry_ == nullptr) return invalid<WeaponDefinition>("weapon definition registry is not bound", "registry");
    return definitionFor(*registry_, definitionHandle(), identity().definition);
}

void WeaponDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool WeaponDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> WeaponDefinitionRuntime::applyTo(WeaponEntity* entity) const {
    if (entity == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "weapon runtime cannot project to a null WeaponEntity",
                                                                 "entity", {}, "weapon.definition_runtime"));
    auto typed = definition();
    if (!typed) return eve::Result<void>::failure(typed.status());
    try {
        auto definitionOwner       = std::make_shared<const WeaponDefinition>(typed.value());
        auto definitionComponent   = *entity->definition();
        auto binding               = *entity->definitionBinding();
        auto targetState           = *entity->state();
        definitionComponent.owned  = std::move(definitionOwner);
        definitionComponent.def    = definitionComponent.owned.get();
        binding.identity           = identity();
        binding.reloadPolicy       = policy_;
        binding.active             = isActive();
        const auto& source         = state();
        targetState.resource       = source.resource;
        targetState.cooldown       = source.cooldown;
        targetState.burstRemaining = source.burstRemaining;
        targetState.burstTimer     = source.burstTimer;
        targetState.jammed         = source.jammed;
        targetState.stage          = source.stage;
        targetState.stageTimer     = source.stageTimer;
        targetState.stages         = &definitionComponent.def->stages;
        targetState.currentSpread  = source.currentSpread;
        targetState.recoilPitch    = source.recoilPitch;
        targetState.recoilYaw      = source.recoilYaw;
        targetState.selector       = source.selector;
        targetState.aiming         = source.aiming;
        using std::swap;
        swap(*entity->definition(), definitionComponent);
        swap(*entity->definitionBinding(), binding);
        swap(*entity->state(), targetState);
        return eve::Result<void>::success();
    } catch (const std::exception&) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                 "weapon runtime projection allocation failed",
                                                                 "entity", {}, "weapon.definition_runtime"));
    }
}

eve::Result<eve::definition::ReloadOutcome> WeaponDefinitionRuntime::reload(eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr)
        return invalid<eve::definition::ReloadOutcome>("weapon definition registry is not bound", "registry");
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto typed = definitionFor(*registry_, next.value(), identity().definition);
    if (!typed) return eve::Result<eve::definition::ReloadOutcome>::failure(typed.status());
    const WeaponRuntimeState defaults  = initialState(typed.value());
    const float              maxSpread = std::max(defaults.currentSpread, typed.value().spreadMax);
    auto rebuild = [defaults, maxSpread](const WeaponRuntimeState& oldState, const eve::definition::InstanceIdentity&,
                                         const eve::definition::DefinitionHandle&) {
        WeaponRuntimeState rebuilt = oldState;
        if (oldState.resource.kind != defaults.resource.kind) {
            rebuilt.resource = defaults.resource;
        } else {
            rebuilt.resource.kind     = defaults.resource.kind;
            rebuilt.resource.max      = defaults.resource.max;
            rebuilt.resource.regen    = defaults.resource.regen;
            rebuilt.resource.cost     = defaults.resource.cost;
            rebuilt.resource.infinite = defaults.resource.infinite;
            if (rebuilt.resource.infinite) {
                rebuilt.resource.value = 0.f;
            } else {
                rebuilt.resource.value = std::clamp(rebuilt.resource.value, 0.f, rebuilt.resource.max);
            }
            rebuilt.resource.reserve = defaults.resource.reserve < 0 ? 0 : rebuilt.resource.reserve;
        }
        rebuilt.selector      = defaults.selector;
        rebuilt.currentSpread = std::clamp(oldState.currentSpread, 0.f, maxSpread);
        return eve::Result<WeaponRuntimeState>::success(std::move(rebuilt));
    };
    auto result = runtime_.reload(next.value(), policy, defaults, std::move(rebuild));
    if (result.ok()) policy_ = policy;
    return result;
}

eve::Result<eve::SnapshotEnvelope> WeaponDefinitionRuntime::snapshot(
    eve::Revision revision, eve::SimulationTick tick, const eve::SnapshotHashProvider& hashProvider) const {
    const eve::definition::RuntimeStateEncoder<WeaponRuntimeState> encoder = [](const WeaponRuntimeState& value) {
        return eve::Result<eve::Value>::success(encodeState(value));
    };
    return eve::definition::snapshotRuntimeInstance(runtime_, "weapon.runtime", runtimeSchema(), eve::SchemaVersion(1),
                                                    revision, tick, hashProvider, encoder);
}

eve::Result<std::string> WeaponDefinitionRuntime::snapshotJson(eve::Revision revision, eve::SimulationTick tick,
                                                               const eve::SnapshotHashProvider& hashProvider) const {
    auto result = snapshot(revision, tick, hashProvider);
    if (!result) return eve::Result<std::string>::failure(result.status());
    return std::move(result).andThen(
        [](eve::SnapshotEnvelope&& value) { return eve::serializeSnapshotEnvelope(value); });
}

eve::Result<void> WeaponDefinitionRuntime::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                                   const eve::SnapshotHashProvider& hashProvider) {
    if (registry_ == nullptr) return invalid<void>("weapon definition registry is not bound", "registry");
    auto current = currentHandle(*registry_, identity().definition);
    if (!current) return eve::Result<void>::failure(current.status());
    if (current.value() != definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "weapon snapshot cannot restore against a replaced definition",
            "definitionGeneration", {}, "weapon.definition_runtime"));
    const eve::definition::RuntimeStateDecoder<WeaponRuntimeState> decoder = decodeState;
    return eve::definition::restoreRuntimeInstance(runtime_, snapshotValue, "weapon.runtime", runtimeSchema(),
                                                   eve::SchemaVersion(1), hashProvider, decoder);
}

}  // namespace eve::weapon
