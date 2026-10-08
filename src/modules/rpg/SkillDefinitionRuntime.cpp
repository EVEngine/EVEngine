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

eve::LogicalId skillSchema() {
    static const eve::LogicalId value = [] {
        const auto parsed = eve::LogicalId::parse("rpg.skill:runtime");
        if (!parsed) throw std::logic_error("invalid built-in skill snapshot schema");
        return *parsed;
    }();
    return value;
}

eve::Result<eve::definition::DefinitionHandle> currentHandle(eve::definitions::DefinitionRegistry& registry,
                                                             const eve::DefinitionRef&             reference) {
    const auto& logical = reference.id();
    return registry.handle(std::string(logical.namespaceName()), std::string(logical.name()));
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

eve::Result<std::vector<std::string>> readStrings(const eve::Value::Object& object, std::string_view name) {
    const auto* value = field(object, name);
    if (value == nullptr) return eve::Result<std::vector<std::string>>::success({});
    const auto* array = value->getIf<eve::Value::Array>();
    if (array == nullptr)
        return eve::Result<std::vector<std::string>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill field must be an array",
                                   std::string(name), {}, "rpg.skill.definition_runtime"));
    std::vector<std::string> result;
    result.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* text = (*array)[index].getIf<std::string>();
        if (text == nullptr || text->empty())
            return eve::Result<std::vector<std::string>>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "skill array item must be a non-empty string",
                std::string(name) + "[" + std::to_string(index) + "]", {}, "rpg.skill.definition_runtime"));
        result.push_back(*text);
    }
    return eve::Result<std::vector<std::string>>::success(std::move(result));
}

eve::Result<SkillDefinition> parseDefinition(const eve::definitions::Definition& source) {
    auto owned = eve::Value::fromJson(source.json);
    if (!owned) return eve::Result<SkillDefinition>::failure(owned.status());
    const auto* payload = owned.value().getIf<eve::Value::Object>();
    if (payload == nullptr)
        return eve::Result<SkillDefinition>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill definition payload must be an object",
                                   "json", {}, "rpg.skill.definition_runtime"));

    SkillDefinition result;
    result.id          = source.id;
    const auto* target = field(*payload, "targetType");
    if (target != nullptr) {
        const auto* text = target->getIf<std::string>();
        if (text == nullptr || text->empty())
            return eve::Result<SkillDefinition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "skill targetType must be a non-empty string", "targetType", {},
                "rpg.skill.definition_runtime"));
        result.targetType = *text;
    }
    double number = 0.0;
    if (field(*payload, "cooldown") != nullptr) {
        if (!readNumber(*payload, "cooldown", number) || number < 0.0 || number > std::numeric_limits<float>::max())
            return eve::Result<SkillDefinition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "skill cooldown must be finite and non-negative", "cooldown", {},
                "rpg.skill.definition_runtime"));
        result.cooldown = static_cast<float>(number);
    }
    if (field(*payload, "castTime") != nullptr) {
        if (!readNumber(*payload, "castTime", number) || number < 0.0 || number > std::numeric_limits<float>::max())
            return eve::Result<SkillDefinition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "skill castTime must be finite and non-negative", "castTime", {},
                "rpg.skill.definition_runtime"));
        result.castTime = static_cast<float>(number);
    }
    auto effects = readStrings(*payload, "grantedEffects");
    if (!effects) return eve::Result<SkillDefinition>::failure(effects.status());
    result.grantedEffects = std::move(effects).takeValue();
    auto tags             = readStrings(*payload, "tags");
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
            return eve::Result<SkillDefinition>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill extra must be an object", "extra",
                                       {}, "rpg.skill.definition_runtime"));
        for (const auto& [key, value] : *extraObject) {
            const auto* text = value.getIf<std::string>();
            if (text == nullptr)
                return eve::Result<SkillDefinition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill extra values must be strings",
                                           "extra." + key, {}, "rpg.skill.definition_runtime"));
            result.extra.emplace(key, *text);
        }
    }
    const auto* costs = field(*payload, "costs");
    if (costs != nullptr) {
        const auto* array = costs->getIf<eve::Value::Array>();
        if (array == nullptr)
            return eve::Result<SkillDefinition>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                "skill costs must be an array", "costs",
                                                                                {}, "rpg.skill.definition_runtime"));
        std::vector<eve::resource::ResourceCost> items;
        for (std::size_t index = 0; index < array->size(); ++index) {
            const auto* item = (*array)[index].getIf<eve::Value::Object>();
            if (item == nullptr)
                return eve::Result<SkillDefinition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill cost must be an object",
                                           "costs[" + std::to_string(index) + "]", {}, "rpg.skill.definition_runtime"));
            const auto* resource = field(*item, "attribute");
            const auto* amount   = field(*item, "amount");
            const auto* name     = resource ? resource->getIf<std::string>() : nullptr;
            const auto* integer  = amount ? amount->getIf<std::int64_t>() : nullptr;
            if (name == nullptr || name->empty() || integer == nullptr || *integer <= 0)
                return eve::Result<SkillDefinition>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "skill cost requires a positive attribute amount",
                    "costs[" + std::to_string(index) + "]", {}, "rpg.skill.definition_runtime"));
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
    if (object == nullptr)
        return eve::Result<SkillRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill runtime state must be an object",
                                   "state", {}, "rpg.skill.definition_runtime"));
    static const std::set<std::string> fields = {"cooldownRemaining", "learned"};
    for (const auto& [name, unused] : *object) {
        (void)unused;
        if (!fields.contains(name))
            return eve::Result<SkillRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown skill runtime state field",
                                       "state." + name, {}, "rpg.skill.definition_runtime"));
    }
    if (!object->contains("cooldownRemaining") || !object->contains("learned"))
        return eve::Result<SkillRuntimeState>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill runtime state is missing a required field", "state", {},
            "rpg.skill.definition_runtime"));
    const auto* cooldown = object->at("cooldownRemaining").getIf<double>();
    const auto* learned  = object->at("learned").getIf<bool>();
    if (cooldown == nullptr || learned == nullptr || !std::isfinite(*cooldown) || *cooldown < 0.0 ||
        *cooldown > std::numeric_limits<float>::max())
        return eve::Result<SkillRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill cooldown state is invalid",
                                   "state.cooldownRemaining", {}, "rpg.skill.definition_runtime"));
    return eve::Result<SkillRuntimeState>::success(SkillRuntimeState{static_cast<float>(*cooldown), *learned});
}

}  // namespace

eve::Result<SkillDefinitionRuntime> SkillDefinitionRuntime::create(eve::definitions::DefinitionRegistry& registry,
                                                                   eve::DefinitionRef                    definition,
                                                                   eve::PersistentId                     instanceId,
                                                                   eve::definition::ReloadPolicy         policy) {
    if (!definition.id().isValid() || definition.id().namespaceName() != "rpg.skill")
        return eve::Result<SkillDefinitionRuntime>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "skill definition reference must use rpg.skill namespace",
            "definition", {}, "rpg.skill.definition_runtime"));
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

eve::Result<SkillDefinitionRuntime> SkillDefinitionRuntime::create(eve::PersistentId             instanceId,
                                                                   std::string_view              skillId,
                                                                   eve::definition::ReloadPolicy policy) {
    auto logical = eve::LogicalId::fromParts("rpg.skill", skillId);
    if (!logical)
        return eve::Result<SkillDefinitionRuntime>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill id is not a valid logical name",
                                   "skillId", {}, "rpg.skill.definition_runtime"));
    auto reference = eve::DefinitionRef::fromId(*logical);
    if (!reference) return eve::Result<SkillDefinitionRuntime>::failure(reference.status());
    return create(eve::rpg::SkillRegistry::definitionRegistry(), std::move(reference).takeValue(), instanceId, policy);
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
        return eve::Result<SkillDefinition>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill definition registry is not bound",
                                   "registry", {}, "rpg.skill.definition_runtime"));
    auto resolved = registry_->resolveHandle(definitionHandle());
    if (!resolved) return eve::Result<SkillDefinition>::failure(resolved.status());
    return parseDefinition(resolved.value().get());
}

void SkillDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool SkillDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> SkillDefinitionRuntime::applyTo(RPGActor* actor) const {
    if (actor == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "skill runtime cannot project to a null RPGActor",
                                                                 "actor", {}, "rpg.skill.definition_runtime"));
    auto              candidate = *actor->skills().operator->();
    const std::string id(identity().definition.id().name());
    if (state().learned)
        candidate.known[id] = SkillRuntime{state().cooldownRemaining};
    else
        candidate.known.erase(id);
    using std::swap;
    swap(*actor->skills().operator->(), candidate);
    return eve::Result<void>::success();
}

eve::Result<eve::definition::ReloadOutcome> SkillDefinitionRuntime::reload(eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr)
        return eve::Result<eve::definition::ReloadOutcome>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "skill definition registry is not bound",
                                   "registry", {}, "rpg.skill.definition_runtime"));
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto resolved = registry_->resolveHandle(next.value());
    if (!resolved) return eve::Result<eve::definition::ReloadOutcome>::failure(resolved.status());
    auto typed = parseDefinition(resolved.value().get());
    if (!typed) return eve::Result<eve::definition::ReloadOutcome>::failure(typed.status());
    const float       nextCooldown = typed.value().cooldown;
    SkillRuntimeState defaults{};
    auto rebuild = [nextCooldown](const SkillRuntimeState& oldState, const eve::definition::InstanceIdentity&,
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
    eve::Revision revision, eve::SimulationTick tick, const eve::SnapshotHashProvider& hashProvider) const {
    const eve::definition::RuntimeStateEncoder<SkillRuntimeState> encoder = [](const SkillRuntimeState& value) {
        return eve::Result<eve::Value>::success(encodeState(value));
    };
    return eve::definition::snapshotRuntimeInstance(runtime_, "rpg.skill.runtime", skillSchema(), eve::SchemaVersion(1),
                                                    revision, tick, hashProvider, encoder);
}

eve::Result<std::string> SkillDefinitionRuntime::snapshotJson(eve::Revision revision, eve::SimulationTick tick,
                                                              const eve::SnapshotHashProvider& hashProvider) const {
    auto result = snapshot(revision, tick, hashProvider);
    if (!result) return eve::Result<std::string>::failure(result.status());
    return std::move(result).andThen(
        [](eve::SnapshotEnvelope&& value) { return eve::serializeSnapshotEnvelope(value); });
}

eve::Result<void> SkillDefinitionRuntime::restore(const eve::SnapshotEnvelope&     snapshotValue,
                                                  const eve::SnapshotHashProvider& hashProvider) {
    if (registry_ == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "skill definition registry is not bound", "registry",
                                                                 {}, "rpg.skill.definition_runtime"));
    auto current = currentHandle(*registry_, identity().definition);
    if (!current) return eve::Result<void>::failure(current.status());
    if (current.value() != definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "skill snapshot cannot restore against a replaced definition",
            "definitionGeneration", {}, "rpg.skill.definition_runtime"));
    const eve::definition::RuntimeStateDecoder<SkillRuntimeState> decoder = decodeState;
    return eve::definition::restoreRuntimeInstance(runtime_, snapshotValue, "rpg.skill.runtime", skillSchema(),
                                                   eve::SchemaVersion(1), hashProvider, decoder);
}

}  // namespace eve::rpg
