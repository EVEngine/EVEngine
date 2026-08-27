#include "vehicle/VehicleDefinitionRuntime.h"

#include "common/Value.h"
#include "vehicle/Vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace eve::vehicle {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "vehicle.definition_runtime"));
}

eve::LogicalId vehicleSchema() {
    static const eve::LogicalId value = [] {
        const auto parsed = eve::LogicalId::parse("vehicle:runtime");
        if (!parsed) throw std::logic_error("invalid built-in vehicle snapshot schema");
        return *parsed;
    }();
    return value;
}

eve::Result<eve::definition::DefinitionHandle> currentHandle(
    eve::definitions::DefinitionRegistry& registry, const eve::DefinitionRef& reference) {
    const auto& logical = reference.id();
    return registry.handle(std::string(logical.namespaceName()),
                                  std::string(logical.name()));
}

const eve::Value* field(const eve::Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

bool readString(const eve::Value::Object& object, std::string_view name, std::string& result,
                bool required = false) {
    const auto* value = field(object, name);
    if (value == nullptr) return !required;
    const auto* text = value->getIf<std::string>();
    if (text == nullptr || (required && text->empty())) return false;
    result = *text;
    return true;
}

bool readNumber(const eve::Value::Object& object, std::string_view name, float& result,
                float fallback) {
    const auto* value = field(object, name);
    if (value == nullptr) {
        result = fallback;
        return true;
    }
    double number = 0.0;
    if (const auto* real = value->getIf<double>()) number = *real;
    else if (const auto* integer = value->getIf<std::int64_t>()) number = static_cast<double>(*integer);
    else return false;
    if (!std::isfinite(number) || number < 0.0 || number > std::numeric_limits<float>::max())
        return false;
    result = static_cast<float>(number);
    return true;
}

bool readSignedNumber(const eve::Value::Object& object, std::string_view name, float& result,
                      float fallback) {
    const auto* value = field(object, name);
    if (value == nullptr) {
        result = fallback;
        return true;
    }
    double number = 0.0;
    if (const auto* real = value->getIf<double>()) number = *real;
    else if (const auto* integer = value->getIf<std::int64_t>()) number = static_cast<double>(*integer);
    else return false;
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        return false;
    result = static_cast<float>(number);
    return true;
}

bool readBool(const eve::Value::Object& object, std::string_view name, bool& result,
              bool fallback) {
    const auto* value = field(object, name);
    if (value == nullptr) {
        result = fallback;
        return true;
    }
    const auto* boolean = value->getIf<bool>();
    if (boolean == nullptr) return false;
    result = *boolean;
    return true;
}

}  // namespace

eve::Result<VehicleDefinition> parseVehicleDefinition(const eve::definitions::Definition& source) {
    auto parsed = eve::Value::fromJson(source.json);
    if (!parsed) return eve::Result<VehicleDefinition>::failure(parsed.status());
    const auto* object = parsed.value().getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<VehicleDefinition>("vehicle definition must be an object", "json");

    VehicleDefinition result;
    result.id = source.id;
    if (!readString(*object, "category", result.category) ||
        !readString(*object, "mobility", result.mobility))
        return invalid<VehicleDefinition>("vehicle category and mobility must be strings", "definition");
    if (!readNumber(*object, "maxSpeed", result.maxSpeed, result.maxSpeed) ||
        !readNumber(*object, "accel", result.accel, result.accel) ||
        !readNumber(*object, "turnRate", result.turnRate, result.turnRate) ||
        !readNumber(*object, "radius", result.radius, result.radius) ||
        !readNumber(*object, "maxHealth", result.maxHealth, result.maxHealth) ||
        result.radius <= 0.f || result.maxHealth < 0.f)
        return invalid<VehicleDefinition>("vehicle numeric definition field is invalid", "definition");

    if (const auto* physics = field(*object, "physics")) {
        const auto* physicsObject = physics->getIf<eve::Value::Object>();
        if (physicsObject == nullptr)
            return invalid<VehicleDefinition>("vehicle physics must be an object", "physics");
        if (!readNumber(*physicsObject, "maxSpeed", result.maxSpeed, result.maxSpeed) ||
            !readNumber(*physicsObject, "accel", result.accel, result.accel) ||
            !readNumber(*physicsObject, "turnRate", result.turnRate, result.turnRate) ||
            !readNumber(*physicsObject, "radius", result.radius, result.radius))
            return invalid<VehicleDefinition>("vehicle physics numeric field is invalid", "physics");
    }

    if (const auto* zones = field(*object, "armorZones")) {
        const auto* array = zones->getIf<eve::Value::Array>();
        if (array == nullptr) return invalid<VehicleDefinition>("armorZones must be an array", "armorZones");
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<eve::Value::Object>();
            if (item == nullptr) return invalid<VehicleDefinition>("armor zone must be an object", "armorZones");
            ArmorZone zone;
            if (!readString(*item, "name", zone.name) || !readString(*item, "node", zone.node) ||
                !readNumber(*item, "mult", zone.mult, zone.mult) || zone.name.empty())
                return invalid<VehicleDefinition>("armor zone fields are invalid", "armorZones");
            result.armorZones.push_back(std::move(zone));
        }
    }

    if (const auto* mounts = field(*object, "mounts")) {
        const auto* array = mounts->getIf<eve::Value::Array>();
        if (array == nullptr) return invalid<VehicleDefinition>("mounts must be an array", "mounts");
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<eve::Value::Object>();
            if (item == nullptr) return invalid<VehicleDefinition>("mount must be an object", "mounts");
            MountDef mount;
            mount.name = "mount" + std::to_string(index);
            if (!readString(*item, "name", mount.name) || !readString(*item, "weapon", mount.weapon) ||
                !readString(*item, "type", mount.type) || !readString(*item, "aimMode", mount.aimMode) ||
                !readSignedNumber(*item, "yawMin", mount.yawMin, mount.yawMin) ||
                !readSignedNumber(*item, "yawMax", mount.yawMax, mount.yawMax) ||
                !readSignedNumber(*item, "pitchMin", mount.pitchMin, mount.pitchMin) ||
                !readSignedNumber(*item, "pitchMax", mount.pitchMax, mount.pitchMax) ||
                !readNumber(*item, "rotSpeed", mount.rotSpeed, mount.rotSpeed) ||
                !readNumber(*item, "firingArc", mount.firingArc, mount.firingArc))
                return invalid<VehicleDefinition>("mount definition field is invalid", "mounts");
            if (const auto* limits = field(*item, "limits")) {
                const auto* values = limits->getIf<eve::Value::Array>();
                if (values == nullptr || values->size() < 4)
                    return invalid<VehicleDefinition>("mount limits must contain four numbers", "mounts.limits");
                float* output[] = {&mount.yawMin, &mount.yawMax, &mount.pitchMin, &mount.pitchMax};
                for (std::size_t limit = 0; limit < 4; ++limit) {
                    const auto* real = (*values)[limit].getIf<double>();
                    const auto* integer = (*values)[limit].getIf<std::int64_t>();
                    const double number = real ? *real : integer ? static_cast<double>(*integer)
                                                                  : std::numeric_limits<double>::quiet_NaN();
                    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
                        number > std::numeric_limits<float>::max())
                        return invalid<VehicleDefinition>("mount limit is not finite", "mounts.limits");
                    *output[limit] = static_cast<float>(number);
                }
            }
            result.mounts.push_back(std::move(mount));
        }
    }

    if (const auto* suspension = field(*object, "suspension")) {
        const auto* value = suspension->getIf<eve::Value::Object>();
        if (value == nullptr) return invalid<VehicleDefinition>("suspension must be an object", "suspension");
        if (!readNumber(*value, "maxTravel", result.suspension.maxTravel, result.suspension.maxTravel) ||
            !readNumber(*value, "driveForce", result.suspension.driveForce, result.suspension.driveForce) ||
            !readNumber(*value, "lateralGrip", result.suspension.lateralGrip, result.suspension.lateralGrip))
            return invalid<VehicleDefinition>("suspension numeric field is invalid", "suspension");
        if (const auto* wheels = field(*value, "wheels")) {
            const auto* array = wheels->getIf<eve::Value::Array>();
            if (array == nullptr) return invalid<VehicleDefinition>("suspension wheels must be an array", "suspension.wheels");
            for (std::size_t index = 0; index < array->size(); ++index) {
                const auto* item = (*array)[index].getIf<eve::Value::Object>();
                if (item == nullptr) return invalid<VehicleDefinition>("wheel must be an object", "suspension.wheels");
                SuspensionWheel wheel;
                if (!readSignedNumber(*item, "x", wheel.x, wheel.x) || !readSignedNumber(*item, "y", wheel.y, wheel.y) ||
                    !readSignedNumber(*item, "z", wheel.z, wheel.z) || !readNumber(*item, "radius", wheel.radius, wheel.radius) ||
                    !readNumber(*item, "rest", wheel.restLength, wheel.restLength) ||
                    !readNumber(*item, "stiffness", wheel.stiffness, wheel.stiffness) ||
                    !readNumber(*item, "damping", wheel.damping, wheel.damping) ||
                    !readBool(*item, "drive", wheel.drive, wheel.drive) || !readBool(*item, "steer", wheel.steer, wheel.steer))
                    return invalid<VehicleDefinition>("wheel definition field is invalid", "suspension.wheels");
                result.suspension.wheels.push_back(wheel);
            }
        }
    }

    if (const auto* seats = field(*object, "seats")) {
        const auto* array = seats->getIf<eve::Value::Array>();
        if (array == nullptr) return invalid<VehicleDefinition>("seats must be an array", "seats");
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<eve::Value::Object>();
            if (item == nullptr) return invalid<VehicleDefinition>("seat must be an object", "seats");
            SeatDef seat;
            seat.name = "passenger" + std::to_string(index);
            if (!readString(*item, "name", seat.name) || !readString(*item, "driver", seat.driver) ||
                !readString(*item, "cameraMode", seat.cameraMode))
                return invalid<VehicleDefinition>("seat definition field is invalid", "seats");
            if (const auto* mount = field(*item, "mountIndex")) {
                const auto* integer = mount->getIf<std::int64_t>();
                if (integer == nullptr || *integer < std::numeric_limits<int>::min() ||
                    *integer > std::numeric_limits<int>::max())
                    return invalid<VehicleDefinition>("seat mountIndex is invalid", "seats.mountIndex");
                seat.mountIndex = static_cast<int>(*integer);
            }
            result.seats.push_back(std::move(seat));
        }
    }
    if (const auto* tags = field(*object, "tags")) {
        const auto* array = tags->getIf<eve::Value::Array>();
        if (array == nullptr) return invalid<VehicleDefinition>("vehicle tags must be an array", "tags");
        for (const auto& item : *array) {
            const auto* text = item.getIf<std::string>();
            if (text == nullptr || text->empty()) return invalid<VehicleDefinition>("vehicle tag is invalid", "tags");
            result.tags.push_back(*text);
        }
    }
    return eve::Result<VehicleDefinition>::success(std::move(result));
}

namespace {

eve::Value encodeState(const VehicleRuntimeState& state) {
    return eve::Value(eve::Value::Object{
        {"destroyed", eve::Value(state.destroyed)},
        {"faction", eve::Value(state.faction)},
        {"heading", eve::Value(static_cast<double>(state.heading))},
        {"health", eve::Value(static_cast<double>(state.health))},
        {"speed", eve::Value(static_cast<double>(state.speed))},
        {"x", eve::Value(static_cast<double>(state.x))},
        {"y", eve::Value(static_cast<double>(state.y))},
    });
}

eve::Result<VehicleRuntimeState> decodeState(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<VehicleRuntimeState>("vehicle runtime state must be an object", "state");
    static const std::set<std::string> fields = {"destroyed", "faction", "heading", "health", "speed", "x", "y"};
    for (const auto& [name, unused] : *object) {
        (void)unused;
        if (!fields.contains(name)) return invalid<VehicleRuntimeState>("unknown vehicle runtime state field", "state." + name);
    }
    for (const auto& name : fields)
        if (!object->contains(name)) return invalid<VehicleRuntimeState>("vehicle runtime state is missing a field", "state." + name);
    auto number = [&](std::string_view name, float& output, bool allowNegative) -> bool {
        const auto* real = object->at(std::string(name)).getIf<double>();
        if (real == nullptr || !std::isfinite(*real) ||
            (!allowNegative && *real < 0.0) || *real < -std::numeric_limits<float>::max() ||
            *real > std::numeric_limits<float>::max()) return false;
        output = static_cast<float>(*real);
        return true;
    };
    const auto* destroyed = object->at("destroyed").getIf<bool>();
    const auto* faction = object->at("faction").getIf<std::string>();
    VehicleRuntimeState result;
    if (destroyed == nullptr || faction == nullptr || !number("heading", result.heading, false) ||
        !number("health", result.health, false) || !number("speed", result.speed, false) ||
        !number("x", result.x, true) || !number("y", result.y, true))
        return invalid<VehicleRuntimeState>("vehicle runtime state field is invalid", "state");
    result.destroyed = *destroyed;
    result.faction = *faction;
    return eve::Result<VehicleRuntimeState>::success(std::move(result));
}

}  // namespace

eve::Result<VehicleDefinitionRuntime> VehicleDefinitionRuntime::create(
    eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition,
    eve::PersistentId instanceId, eve::definition::ReloadPolicy policy) {
    if (!definition.id().isValid() || definition.id().namespaceName() != "vehicle")
        return invalid<VehicleDefinitionRuntime>("vehicle definition reference must use vehicle namespace", "definition");
    auto handle = currentHandle(registry, definition);
    if (!handle) return eve::Result<VehicleDefinitionRuntime>::failure(handle.status());
    auto resolved = registry.resolveHandle(handle.value());
    if (!resolved) return eve::Result<VehicleDefinitionRuntime>::failure(resolved.status());
    auto typed = parseVehicleDefinition(resolved.value().get());
    if (!typed) return eve::Result<VehicleDefinitionRuntime>::failure(typed.status());
    VehicleRuntimeState defaults;
    defaults.health = typed.value().maxHealth;
    auto runtime = eve::definition::RuntimeInstance<VehicleRuntimeState>::create(
        instanceId, std::move(definition), handle.value().generation, defaults);
    if (!runtime) return eve::Result<VehicleDefinitionRuntime>::failure(runtime.status());
    return eve::Result<VehicleDefinitionRuntime>::success(
        VehicleDefinitionRuntime(registry, std::move(runtime).takeValue(), policy));
}

eve::Result<VehicleDefinitionRuntime> VehicleDefinitionRuntime::create(
    eve::definitions::DefinitionRegistry& registry, std::string_view definitionId,
    eve::PersistentId instanceId, eve::definition::ReloadPolicy policy) {
    auto logical = eve::LogicalId::fromParts("vehicle", definitionId);
    if (!logical) return invalid<VehicleDefinitionRuntime>("vehicle id is not a valid logical name", "definitionId");
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference) return eve::Result<VehicleDefinitionRuntime>::failure(reference.status());
    return create(registry, std::move(reference).takeValue(), instanceId, policy);
}

eve::Result<VehicleDefinitionRuntime> VehicleDefinitionRuntime::create(
    Vehicle& module, std::string_view definitionId, eve::PersistentId instanceId,
    eve::definition::ReloadPolicy policy) {
    return create(module.definitionRegistry(), definitionId, instanceId, policy);
}

const eve::definition::InstanceIdentity& VehicleDefinitionRuntime::identity() const noexcept {
    return runtime_.identity();
}

const VehicleRuntimeState& VehicleDefinitionRuntime::state() const noexcept { return runtime_.state(); }

VehicleRuntimeState& VehicleDefinitionRuntime::state() noexcept { return runtime_.state(); }

eve::definition::DefinitionHandle VehicleDefinitionRuntime::definitionHandle() const noexcept {
    return runtime_.identity().definitionHandle();
}

eve::Result<VehicleDefinition> VehicleDefinitionRuntime::definition() const {
    if (registry_ == nullptr) return invalid<VehicleDefinition>("vehicle definition registry is not bound", "registry");
    auto resolved = registry_->resolveHandle(definitionHandle());
    if (!resolved) return eve::Result<VehicleDefinition>::failure(resolved.status());
    return parseVehicleDefinition(resolved.value().get());
}

void VehicleDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool VehicleDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> VehicleDefinitionRuntime::applyTo(VehicleEntity* entity) const {
    if (entity == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "vehicle runtime cannot project to a null VehicleEntity",
            "entity", {}, "vehicle.definition_runtime"));
    auto typed = definition();
    if (!typed) return eve::Result<void>::failure(typed.status());
    try {
        auto definitionOwner = std::make_shared<const VehicleDefinition>(typed.value());
        auto identity = *entity->identity();
        auto definition = *entity->definition();
        auto binding = *entity->definitionBinding();
        auto motion = *entity->motion();
        auto health = *entity->health();
        auto flags = *entity->stateFlags();

        identity.id = this->identity().instanceId.format();
        identity.defId = this->identity().definition.id().name();
        definition.owned = std::move(definitionOwner);
        definition.def = definition.owned.get();
        binding.identity = this->identity();
        binding.reloadPolicy = policy_;
        binding.active = isActive();
        motion.x = state().x;
        motion.y = state().y;
        motion.heading = state().heading;
        motion.speed = state().speed;
        motion.arrived = false;
        health.maxHp = typed.value().maxHealth;
        health.hp = std::clamp(state().health, 0.f, health.maxHp);
        flags.destroyed = state().destroyed;

        using std::swap;
        swap(*entity->identity(), identity);
        swap(*entity->definition(), definition);
        swap(*entity->definitionBinding(), binding);
        swap(*entity->motion(), motion);
        swap(*entity->health(), health);
        swap(*entity->stateFlags(), flags);
        return eve::Result<void>::success();
    } catch (const std::exception&) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "vehicle runtime projection allocation failed", "entity",
            {}, "vehicle.definition_runtime"));
    }
}

eve::Result<eve::definition::ReloadOutcome> VehicleDefinitionRuntime::reload(
    eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr) return invalid<eve::definition::ReloadOutcome>("vehicle definition registry is not bound", "registry");
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto resolved = registry_->resolveHandle(next.value());
    if (!resolved) return eve::Result<eve::definition::ReloadOutcome>::failure(resolved.status());
    auto typed = parseVehicleDefinition(resolved.value().get());
    if (!typed) return eve::Result<eve::definition::ReloadOutcome>::failure(typed.status());
    VehicleRuntimeState defaults;
    defaults.health = typed.value().maxHealth;
    const float maxHealth = typed.value().maxHealth;
    auto rebuild = [maxHealth](const VehicleRuntimeState& oldState,
                               const eve::definition::InstanceIdentity&,
                               const eve::definition::DefinitionHandle&) {
        VehicleRuntimeState rebuilt = oldState;
        rebuilt.health = std::clamp(oldState.health, 0.f, maxHealth);
        return eve::Result<VehicleRuntimeState>::success(std::move(rebuilt));
    };
    auto result = runtime_.reload(next.value(), policy, defaults, std::move(rebuild));
    if (result.ok()) policy_ = policy;
    return result;
}

eve::Result<eve::SnapshotEnvelope> VehicleDefinitionRuntime::snapshot(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    const eve::definition::RuntimeStateEncoder<VehicleRuntimeState> encoder =
        [](const VehicleRuntimeState& value) {
            return eve::Result<eve::Value>::success(encodeState(value));
        };
    return eve::definition::snapshotRuntimeInstance(
        runtime_, "vehicle.runtime", vehicleSchema(), eve::SchemaVersion(1), revision, tick,
        hashProvider, encoder);
}

eve::Result<std::string> VehicleDefinitionRuntime::snapshotJson(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    auto result = snapshot(revision, tick, hashProvider);
    if (!result) return eve::Result<std::string>::failure(result.status());
    return std::move(result).andThen(
        [](eve::SnapshotEnvelope&& value) { return eve::serializeSnapshotEnvelope(value); });
}

eve::Result<void> VehicleDefinitionRuntime::restore(
    const eve::SnapshotEnvelope& snapshotValue, const eve::SnapshotHashProvider& hashProvider) {
    if (registry_ == nullptr) return invalid<void>("vehicle definition registry is not bound", "registry");
    auto current = currentHandle(*registry_, identity().definition);
    if (!current) return eve::Result<void>::failure(current.status());
    if (current.value() != definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle,
            "vehicle snapshot cannot restore against a replaced definition", "definitionGeneration", {},
            "vehicle.definition_runtime"));
    const eve::definition::RuntimeStateDecoder<VehicleRuntimeState> decoder = decodeState;
    return eve::definition::restoreRuntimeInstance(
        runtime_, snapshotValue, "vehicle.runtime", vehicleSchema(), eve::SchemaVersion(1),
        hashProvider, decoder);
}

}  // namespace eve::vehicle
