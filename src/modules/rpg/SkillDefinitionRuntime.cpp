#include "rpg/SkillDefinitionRuntime.h"

#include "common/Value.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillConditionCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "rpg.skill.definition_runtime"));
}

eve::LogicalId skillSchema() {
    static const eve::LogicalId value = [] {
        const auto parsed = eve::LogicalId::parse("rpg.skill:runtime");
        if (!parsed) throw std::logic_error("invalid built-in skill snapshot schema");
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

bool readNumber(const eve::Value::Object& object, std::string_view name, double& result) {
    const auto* value = field(object, name);
    if (const auto* number = value ? value->getIf<double>() : nullptr) {
        result = *number;
        return std::isfinite(result);
    }
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        result = static_cast<double>(*integer);
        return std::isfinite(result);
    }
    return false;
}

eve::Result<std::vector<std::string>> readStrings(const eve::Value::Object& object,
                                                  std::string_view name) {
    const auto* value = field(object, name);
    if (value == nullptr) return eve::Result<std::vector<std::string>>::success({});
    const auto* array = value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return invalid<std::vector<std::string>>("skill field must be an array", std::string(name));
    std::vector<std::string> result;
    result.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* text = (*array)[index].getIf<std::string>();
        if (text == nullptr || text->empty())
            return invalid<std::vector<std::string>>("skill array item must be a non-empty string",
                                                     std::string(name) + "[" + std::to_string(index) + "]");
        result.push_back(*text);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(result));
}

eve::Result<SkillDefinition> parseDefinition(const eve::definitions::Definition& source) {
    auto owned = eve::Value::fromJson(source.json);
    if (!owned) return eve::Result<SkillDefinition>::failure(owned.status());
    const auto* payload = owned.value().getIf<eve::Value::Object>();
    if (payload == nullptr)
        return invalid<SkillDefinition>("skill definition payload must be an object", "json");

    SkillDefinition result;
    result.id = source.id;
    const auto* target = field(*payload, "targetType");
    if (target != nullptr) {
        const auto* text = target->getIf<std::string>();
        if (text == nullptr || text->empty())
            return invalid<SkillDefinition>("skill targetType must be a non-empty string", "targetType");
        result.targetType = *text;
    }
    double number = 0.0;
    if (field(*payload, "cooldown") != nullptr) {
        if (!readNumber(*payload, "cooldown", number) || number < 0.0 || number > std::numeric_limits<float>::max())
            return invalid<SkillDefinition>("skill cooldown must be finite and non-negative", "cooldown");
        result.cooldown = static_cast<float>(number);
    }
    if (field(*payload, "castTime") != nullptr) {
        if (!readNumber(*payload, "castTime", number) || number < 0.0 || number > std::numeric_limits<float>::max())
            return invalid<SkillDefinition>("skill castTime must be finite and non-negative", "castTime");
        result.castTime = static_cast<float>(number);
    }
    auto effects = readStrings(*payload, "grantedEffects");
    if (!effects) return eve::Result<SkillDefinition>::failure(effects.status());
    result.grantedEffects = std::move(effects).takeValue();
    auto tags = readStrings(*payload, "tags");
    if (!tags) return eve::Result<SkillDefinition>::failure(tags.status());
    result.tags = std::move(tags).takeValue();
    if (const auto* condition = field(*payload, "castCondition")) {
        auto decoded = decodeSkillCondition(*condition);
        if (!decoded) return eve::Result<SkillDefinition>::failure(decoded.status());
        result.castCondition = std::move(decoded).takeValue();
    }
    auto extras = field(*payload, "extra");
    if (extras != nullptr) {
        const auto* extraObject = extras->getIf<eve::Value::Object>();
        if (extraObject == nullptr)
            return invalid<SkillDefinition>("skill extra must be an object", "extra");
        for (const auto& [key, value] : *extraObject) {
            const auto* text = value.getIf<std::string>();
            if (text == nullptr)
                return invalid<SkillDefinition>("skill extra values must be strings", "extra." + key);
            result.extra.emplace(key, *text);
        }
    }
    const auto* costs = field(*payload, "costs");
    if (costs != nullptr) {
        const auto* array = costs->getIf<eve::Value::Array>();
        if (array == nullptr)
            return invalid<SkillDefinition>("skill costs must be an array", "costs");
        std::vector<eve::resource::ResourceCost> items;
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<eve::Value::Object>();
            if (item == nullptr)
                return invalid<SkillDefinition>("skill cost must be an object", "costs[" + std::to_string(index) + "]");
            const auto* resource = field(*item, "attribute");
            const auto* amount = field(*item, "amount");
            const auto* name = resource ? resource->getIf<std::string>() : nullptr;
            const auto* integer = amount ? amount->getIf<std::int64_t>() : nullptr;
            if (name == nullptr || name->empty() || integer == nullptr || *integer <= 0)
                return invalid<SkillDefinition>("skill cost requires a positive attribute amount",
                                                 "costs[" + std::to_string(index) + "]");
            auto cost = eve::resource::ResourceCost::create(*name, *integer);
            if (!cost) return eve::Result<SkillDefinition>::failure(cost.status());
            items.push_back(std::move(cost).takeValue());
        }
        if (!items.empty()) {
            auto cost = eve::resource::CostSpec::create(std::move(items));
            if (!cost) return eve::Result<SkillDefinition>::failure(cost.status());
            result.cost = std::move(cost).takeValue();
        }
    }
    return eve::Result<SkillDefinition>::success(std::move(result));
}

eve::Value encodeState(const SkillRuntimeState& state) {
    return eve::Value(eve::Value::Object{
        {"cooldownRemaining", eve::Value(static_cast<double>(state.cooldownRemaining))},
        {"learned", eve::Value(state.learned)},
    });
}

eve::Result<SkillRuntimeState> decodeState(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (object == nullptr) return invalid<SkillRuntimeState>("skill runtime state must be an object", "state");
    static const std::set<std::string> fields = {"cooldownRemaining", "learned"};
    for (const auto& [name, unused] : *object) {
        (void)unused;
        if (!fields.contains(name)) return invalid<SkillRuntimeState>("unknown skill runtime state field", "state." + name);
    }
    if (!object->contains("cooldownRemaining") || !object->contains("learned"))
        return invalid<SkillRuntimeState>("skill runtime state is missing a required field", "state");
    const auto* cooldown = object->at("cooldownRemaining").getIf<double>();
    const auto* learned = object->at("learned").getIf<bool>();
    if (cooldown == nullptr || learned == nullptr || !std::isfinite(*cooldown) || *cooldown < 0.0 ||
        *cooldown > std::numeric_limits<float>::max())
        return invalid<SkillRuntimeState>("skill cooldown state is invalid", "state.cooldownRemaining");
    return eve::Result<SkillRuntimeState>::success(
        SkillRuntimeState{static_cast<float>(*cooldown), *learned});
}

}  // namespace

eve::Result<SkillDefinitionRuntime> SkillDefinitionRuntime::create(
    eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition,
    eve::PersistentId instanceId, eve::definition::ReloadPolicy policy) {
    if (!definition.id().isValid() || definition.id().namespaceName() != "rpg.skill")
        return invalid<SkillDefinitionRuntime>("skill definition reference must use rpg.skill namespace",
                                                "definition");
    auto handle = currentHandle(registry, definition);
    if (!handle) return eve::Result<SkillDefinitionRuntime>::failure(handle.status());
    auto resolved = registry.resolveHandle(handle.value());
    if (!resolved) return eve::Result<SkillDefinitionRuntime>::failure(resolved.status());
    auto typed = parseDefinition(resolved.value().get());
    if (!typed) return eve::Result<SkillDefinitionRuntime>::failure(typed.status());
    auto runtime = eve::definition::RuntimeInstance<SkillRuntimeState>::create(
        instanceId, std::move(definition), handle.value().generation, SkillRuntimeState{});
    if (!runtime) return eve::Result<SkillDefinitionRuntime>::failure(runtime.status());
    return eve::Result<SkillDefinitionRuntime>::success(
        SkillDefinitionRuntime(registry, std::move(runtime).takeValue(), policy));
}

eve::Result<SkillDefinitionRuntime> SkillDefinitionRuntime::create(
    eve::PersistentId instanceId, std::string_view skillId,
    eve::definition::ReloadPolicy policy) {
    auto logical = eve::LogicalId::fromParts("rpg.skill", skillId);
    if (!logical)
        return invalid<SkillDefinitionRuntime>("skill id is not a valid logical name", "skillId");
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference) return eve::Result<SkillDefinitionRuntime>::failure(reference.status());
    return create(eve::rpg::SkillRegistry::definitionRegistry(),
                  std::move(reference).takeValue(), instanceId, policy);
}

const eve::definition::InstanceIdentity& SkillDefinitionRuntime::identity() const noexcept {
    return runtime_.identity();
}

const SkillRuntimeState& SkillDefinitionRuntime::state() const noexcept { return runtime_.state(); }

SkillRuntimeState& SkillDefinitionRuntime::state() noexcept { return runtime_.state(); }

eve::definition::DefinitionHandle SkillDefinitionRuntime::definitionHandle() const noexcept {
    return runtime_.identity().definitionHandle();
}

eve::Result<SkillDefinition> SkillDefinitionRuntime::definition() const {
    if (registry_ == nullptr)
        return invalid<SkillDefinition>("skill definition registry is not bound", "registry");
    auto resolved = registry_->resolveHandle(definitionHandle());
    if (!resolved) return eve::Result<SkillDefinition>::failure(resolved.status());
    return parseDefinition(resolved.value().get());
}

void SkillDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool SkillDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> SkillDefinitionRuntime::applyTo(RPGActor* actor) const {
    if (actor == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill runtime cannot project to a null RPGActor",
            "actor", {}, "rpg.skill.definition_runtime"));
    auto candidate = *actor->skills().operator->();
    const std::string id(identity().definition.id().name());
    if (state().learned)
        candidate.known[id] = SkillRuntime{state().cooldownRemaining};
    else
        candidate.known.erase(id);
    using std::swap;
    swap(*actor->skills().operator->(), candidate);
    return eve::Result<void>::success();
}

eve::Result<eve::definition::ReloadOutcome> SkillDefinitionRuntime::reload(
    eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr)
        return invalid<eve::definition::ReloadOutcome>("skill definition registry is not bound", "registry");
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto resolved = registry_->resolveHandle(next.value());
    if (!resolved) return eve::Result<eve::definition::ReloadOutcome>::failure(resolved.status());
    auto typed = parseDefinition(resolved.value().get());
    if (!typed) return eve::Result<eve::definition::ReloadOutcome>::failure(typed.status());
    const float nextCooldown = typed.value().cooldown;
    SkillRuntimeState defaults{};
    auto rebuild = [nextCooldown](const SkillRuntimeState& oldState,
                                  const eve::definition::InstanceIdentity&,
                                  const eve::definition::DefinitionHandle&) {
        SkillRuntimeState rebuilt = oldState;
        rebuilt.cooldownRemaining = std::clamp(oldState.cooldownRemaining, 0.f, nextCooldown);
        return eve::Result<SkillRuntimeState>::success(rebuilt);
    };
    auto result = runtime_.reload(next.value(), policy, defaults, std::move(rebuild));
    if (result.ok()) policy_ = policy;
    return result;
}

eve::Result<eve::SnapshotEnvelope> SkillDefinitionRuntime::snapshot(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    const eve::definition::RuntimeStateEncoder<SkillRuntimeState> encoder =
        [](const SkillRuntimeState& value) {
            return eve::Result<eve::Value>::success(encodeState(value));
        };
    return eve::definition::snapshotRuntimeInstance(
        runtime_, "rpg.skill.runtime", skillSchema(), eve::SchemaVersion(1), revision, tick,
        hashProvider, encoder);
}

eve::Result<std::string> SkillDefinitionRuntime::snapshotJson(
    eve::Revision revision, eve::SimulationTick tick,
    const eve::SnapshotHashProvider& hashProvider) const {
    auto result = snapshot(revision, tick, hashProvider);
    if (!result) return eve::Result<std::string>::failure(result.status());
    return std::move(result).andThen(
        [](eve::SnapshotEnvelope&& value) { return eve::serializeSnapshotEnvelope(value); });
}

eve::Result<void> SkillDefinitionRuntime::restore(
    const eve::SnapshotEnvelope& snapshotValue, const eve::SnapshotHashProvider& hashProvider) {
    if (registry_ == nullptr)
        return invalid<void>("skill definition registry is not bound", "registry");
    auto current = currentHandle(*registry_, identity().definition);
    if (!current) return eve::Result<void>::failure(current.status());
    if (current.value() != definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle,
            "skill snapshot cannot restore against a replaced definition", "definitionGeneration", {},
            "rpg.skill.definition_runtime"));
    const eve::definition::RuntimeStateDecoder<SkillRuntimeState> decoder = decodeState;
    return eve::definition::restoreRuntimeInstance(
        runtime_, snapshotValue, "rpg.skill.runtime", skillSchema(), eve::SchemaVersion(1),
        hashProvider, decoder);
}

}  // namespace eve::rpg
