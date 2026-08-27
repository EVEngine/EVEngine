#include "rpg/StatusSystem.h"
#include "rpg/AttributeSystem.h"
#include "rpg/Effect.h"
#include "rpg/RPGActor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::rpg {

namespace {

using LifecycleDefinition = ::eve::effects::EffectDefinition;
using LifecycleInstance   = ::eve::effects::EffectInstance;
using StatusMetadata      = StatusExecutorMetadata;
using ModifierBinding     = std::pair<std::string, std::string>;

std::vector<StatusTickEvent> &tickQueue() {
    static std::vector<StatusTickEvent> q;
    return q;
}

std::vector<StatusChangeEvent> &changeQueue() {
    static std::vector<StatusChangeEvent> q;
    return q;
}

eve::Status rejectedStatus(eve::DiagnosticCode code, std::string message,
                           eve::StatusCode statusCode = eve::StatusCode::Rejected) {
    return eve::Status::failure(statusCode, eve::Diagnostic::error(code, std::move(message)));
}

template <typename T>
eve::Result<T> rejected(eve::DiagnosticCode code, std::string message,
                        eve::StatusCode statusCode = eve::StatusCode::Rejected) {
    return eve::Result<T>::failure(rejectedStatus(code, std::move(message), statusCode));
}

bool isBuiltinStackPolicy(const std::string &policy) {
    return policy == "none" || policy == "refresh" || policy == "extend" || policy == "stack";
}

eve::Result<void> validateRpgDefinition(const EffectDefinition &definition) {
    if (definition.id.empty())
        return rejected<void>(eve::DiagnosticCode::InvalidArgument, "effect definition id must not be empty");
    if (definition.durationPolicy != "instant" && definition.durationPolicy != "duration" &&
        definition.durationPolicy != "infinite") {
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "unknown effect duration policy: " + definition.durationPolicy);
    }
    if (!std::isfinite(definition.duration) || definition.duration < 0.0f)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "effect definition duration must be finite and non-negative");
    if (!std::isfinite(definition.period) || definition.period < 0.0f)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "effect definition period must be finite and non-negative");
    if (definition.maxStacks < 0)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument, "effect definition maxStacks must be non-negative");
    if (!isBuiltinStackPolicy(definition.stackPolicy) && !StatusSystem::hasStackPolicy(definition.stackPolicy)) {
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "unknown effect stack policy: " + definition.stackPolicy);
    }

    for (const auto &modifier : definition.modifiers) {
        if (modifier.attribute.empty())
            return rejected<void>(eve::DiagnosticCode::InvalidArgument, "effect modifier attribute must not be empty");
        if (!std::isfinite(modifier.value))
            return rejected<void>(eve::DiagnosticCode::InvalidArgument, "effect modifier value must be finite");

        auto parsed = ::eve::attributes::parseAttributeOperation(modifier.op, modifier.value);
        if (parsed.ok()) {
            std::move(parsed).takeValue();
            continue;
        }
        const eve::Status diagnostic = parsed.status();
        if (!AttributeSystem::customOps().has(modifier.op)) return eve::Result<void>::failure(diagnostic);
        parsed.ignore("custom RPG attribute operation is validated by the registry");
    }

    return eve::Result<void>::success();
}

LifecycleDefinition makeLifecycleDefinition(const EffectDefinition &definition, ::eve::effects::StackMode stackMode,
                                            ::eve::effects::StackCountPolicy stackCountPolicy,
                                            ::eve::effects::DurationPolicy durationPolicy, std::uint32_t stackCount = 1,
                                            double duration = -1.0) {
    LifecycleDefinition result;
    result.id = definition.id;
    // One RPG actor owns one container, so the actor-local subject is enough;
    // the canonical string instance id remains the identity across all maps.
    result.stackKey = definition.id;
    result.priority = 0;
    result.duration =
        duration >= 0.0 ? duration : (definition.durationPolicy == "duration" ? double(definition.duration) : 0.0);
    result.magnitude         = 0.0;
    result.stackCount        = stackCount;
    result.policy.stackMode  = stackMode;
    result.policy.stackCount = stackCountPolicy;
    result.policy.duration   = durationPolicy;
    result.policy.magnitude  = ::eve::effects::MagnitudePolicy::Keep;
    result.policy.overflow   = ::eve::effects::OverflowPolicy::Reject;
    result.policy.maxStacks  = definition.maxStacks == 0 ? 0u : static_cast<std::uint32_t>(definition.maxStacks);
    result.tags              = definition.tags;
    for (const auto &[key, value] : definition.extra) result.payload.setString(key, value);
    return result;
}

LifecycleDefinition makeNewLifecycleDefinition(const EffectDefinition &definition) {
    auto result =
        makeLifecycleDefinition(definition, ::eve::effects::StackMode::NewInstance,
                                ::eve::effects::StackCountPolicy::Keep, ::eve::effects::DurationPolicy::Replace);
    result.policy.magnitude = ::eve::effects::MagnitudePolicy::Replace;
    return result;
}

LifecycleInstance *findByType(RPGActor::Statuses &statuses, const std::string &effectId) {
    for (int i = 0; i < statuses.container.effectCount(); ++i) {
        LifecycleInstance *effect = statuses.container.effectAt(i);
        if (effect && effect->type == effectId) return effect;
    }
    return nullptr;
}

int legacyIdFor(const RPGActor::Statuses &statuses, const std::string &canonicalId) {
    const auto it = statuses.legacyIdByEffect.find(canonicalId);
    return it == statuses.legacyIdByEffect.end() ? 0 : it->second;
}

LifecycleInstance *findByLegacyId(RPGActor::Statuses &statuses, int instanceId) {
    const auto mapping = statuses.effectByLegacyId.find(instanceId);
    if (mapping == statuses.effectByLegacyId.end()) return nullptr;
    return statuses.container.find(mapping->second);
}

StatusInstance project(const LifecycleInstance &effect, const StatusMetadata &metadata, int legacyId) {
    StatusInstance result;
    result.instanceId       = legacyId;
    result.effectId         = effect.type;
    result.source           = effect.source;
    result.stacks           = effect.stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                  ? std::numeric_limits<int>::max()
                                  : static_cast<int>(effect.stackCount);
    result.remaining        = static_cast<float>(effect.remaining);
    result.periodAccum      = static_cast<float>(metadata.periodAccum);
    result.appliedModifiers = metadata.appliedModifiers;
    result.props            = metadata.props;
    return result;
}

eve::Result<int> allocateLegacyId(RPGActor::Statuses &statuses) {
    if (statuses.nextInstanceId <= 0)
        return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                             "RPG status legacy instance id space is exhausted", eve::StatusCode::Failed);

    const int allocated = statuses.nextInstanceId;
    if (allocated == std::numeric_limits<int>::max())
        statuses.nextInstanceId = 0;
    else
        ++statuses.nextInstanceId;
    return eve::Result<int>::success(allocated, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> validateCandidate(const StatusInstance &candidate, const StatusInstance &current,
                                    const EffectDefinition &definition) {
    (void)definition;
    if (candidate.instanceId != current.instanceId || candidate.effectId != current.effectId)
        return rejected<void>(eve::DiagnosticCode::InvariantViolation,
                              "custom stack policy cannot change the status identity", eve::StatusCode::Failed);
    if (candidate.stacks <= 0)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "custom stack policy must leave a positive stack count");
    if (!std::isfinite(candidate.remaining) || candidate.remaining < -1.0f)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "custom stack policy produced an invalid remaining duration");
    if (!std::isfinite(candidate.periodAccum) || candidate.periodAccum < 0.0f)
        return rejected<void>(eve::DiagnosticCode::InvalidArgument,
                              "custom stack policy produced an invalid period accumulator");
    return eve::Result<void>::success();
}

eve::Result<void> removeBindings(::eve::attributes::AttributeSet &attributes, const StatusMetadata &metadata) {
    for (const auto &[attribute, modifierId] : metadata.appliedModifiers) {
        (void)attribute;
        auto removed = attributes.removeModifier(modifierId);
        if (!removed.ok()) return eve::Result<void>::failure(removed.status());
    }
    return eve::Result<void>::success();
}

eve::Result<std::vector<ModifierBinding>> prepareModifierBindings(::eve::attributes::AttributeSet &attributes,
                                                                  const EffectDefinition          &definition,
                                                                  const LifecycleInstance         &effect,
                                                                  const StatusMetadata            &oldMetadata) {
    auto removed = removeBindings(attributes, oldMetadata);
    if (!removed.ok()) return eve::Result<std::vector<ModifierBinding>>::failure(removed.status());

    // A definition reload or adapter re-application may change an effect from
    // a direct modifier into a periodic settlement effect. Remove bindings
    // first, then leave the periodic effect to the RPG tick/settlement path.
    if (definition.period > 0.0f) return eve::Result<std::vector<ModifierBinding>>::success({});

    std::vector<ModifierBinding> bindings;
    bindings.reserve(definition.modifiers.size());
    const double stackScale = static_cast<double>(effect.stackCount);
    for (std::size_t index = 0; index < definition.modifiers.size(); ++index) {
        const auto  &spec  = definition.modifiers[index];
        const double value = spec.value * stackScale;
        if (!std::isfinite(value))
            return rejected<std::vector<ModifierBinding>>(eve::DiagnosticCode::InvalidArgument,
                                                          "effect modifier value overflowed during stack scaling");

        AttributeModifier modifier;
        modifier.id        = "status:" + effect.id + ":modifier:" + std::to_string(index);
        modifier.attribute = spec.attribute;
        modifier.source    = "status:" + definition.id + ":" + effect.id;
        modifier.priority  = spec.priority;

        auto parsed = ::eve::attributes::parseAttributeOperation(spec.op, value);
        if (parsed.ok()) {
            const auto normalized = std::move(parsed).takeValue();
            modifier.operation    = normalized.operation;
            modifier.value        = normalized.value;
        } else {
            const eve::Status diagnostic = parsed.status();
            if (!AttributeSystem::customOps().has(spec.op))
                return eve::Result<std::vector<ModifierBinding>>::failure(diagnostic);
            parsed.ignore("custom RPG attribute operation is resolved by AttributeSet");
            modifier.operation = AttributeOperation::Custom;
            modifier.policyId  = spec.op;
            modifier.value     = value;
        }

        auto added = attributes.addModifier(std::move(modifier));
        if (!added.ok()) return eve::Result<std::vector<ModifierBinding>>::failure(added.status());
        const std::string modifierId = std::move(added).takeValue();
        bindings.emplace_back(spec.attribute, modifierId);
    }
    return eve::Result<std::vector<ModifierBinding>>::success(std::move(bindings));
}

void commit(RPGActor *actor, ::eve::effects::EffectContainer &&container,
            std::unordered_map<std::string, StatusMetadata> &&metadata,
            std::unordered_map<std::string, int>            &&legacyIdByEffect,
            std::unordered_map<int, std::string> &&effectByLegacyId, int nextInstanceId,
            ::eve::attributes::AttributeSet &&attributes) {
    auto statuses               = actor->statuses();
    statuses->container         = std::move(container);
    statuses->metadata          = std::move(metadata);
    statuses->legacyIdByEffect  = std::move(legacyIdByEffect);
    statuses->effectByLegacyId  = std::move(effectByLegacyId);
    statuses->nextInstanceId    = nextInstanceId;
    actor->attributes()->values = std::move(attributes);
}

}  // namespace

std::unordered_map<std::string, StatusSystem::ApplyCondition> &StatusSystem::applyConditions() {
    static std::unordered_map<std::string, ApplyCondition> t;
    return t;
}

std::unordered_map<std::string, StatusSystem::StackPolicyFn> &StatusSystem::stackPolicies() {
    static std::unordered_map<std::string, StackPolicyFn> t;
    return t;
}

std::unordered_map<std::string, StatusSystem::LifecycleHook> &StatusSystem::lifecycleHooks() {
    static std::unordered_map<std::string, LifecycleHook> t;
    return t;
}

void StatusSystem::registerApplyCondition(const std::string &name, ApplyCondition fn) {
    if (!name.empty()) applyConditions()[name] = std::move(fn);
}

void StatusSystem::unregisterApplyCondition(const std::string &name) { applyConditions().erase(name); }

bool StatusSystem::hasApplyCondition(const std::string &name) { return applyConditions().count(name) > 0; }

void StatusSystem::clearApplyConditions() { applyConditions().clear(); }

void StatusSystem::registerStackPolicy(const std::string &name, StackPolicyFn fn) {
    if (!name.empty()) stackPolicies()[name] = std::move(fn);
}

void StatusSystem::unregisterStackPolicy(const std::string &name) { stackPolicies().erase(name); }

bool StatusSystem::hasStackPolicy(const std::string &name) { return stackPolicies().count(name) > 0; }

void StatusSystem::clearStackPolicies() { stackPolicies().clear(); }

void StatusSystem::registerLifecycleHook(const std::string &name, LifecycleHook fn) {
    if (!name.empty()) lifecycleHooks()[name] = std::move(fn);
}

void StatusSystem::unregisterLifecycleHook(const std::string &name) { lifecycleHooks().erase(name); }

bool StatusSystem::hasLifecycleHook(const std::string &name) { return lifecycleHooks().count(name) > 0; }

void StatusSystem::clearLifecycleHooks() { lifecycleHooks().clear(); }

void StatusSystem::emitChange(StatusChangeEvent event) {
    changeQueue().push_back(event);
    std::vector<LifecycleHook> hooks;
    hooks.reserve(lifecycleHooks().size());
    for (const auto &[name, hook] : lifecycleHooks()) {
        (void)name;
        if (hook) hooks.push_back(hook);
    }
    for (const auto &hook : hooks) hook(event);
}

eve::Result<int> StatusSystem::apply(RPGActor *actor, const std::string &effectId, const std::string &source) {
    if (!actor) return rejected<int>(eve::DiagnosticCode::InvalidArgument, "status application requires an actor");

    const EffectDefinition *definition = EffectRegistry::find(effectId);
    if (!definition)
        return rejected<int>(eve::DiagnosticCode::NotFound, "effect definition was not found: " + effectId,
                             eve::StatusCode::NotFound);

    auto definitionValidation = validateRpgDefinition(*definition);
    if (!definitionValidation.ok()) return eve::Result<int>::failure(definitionValidation.status());

    for (const auto &[name, condition] : applyConditions()) {
        if (!condition) continue;
        std::string reason;
        if (!condition(actor, *definition, source, reason)) {
            StatusChangeEvent event;
            event.actor                   = actor;
            event.effectId                = effectId;
            event.source                  = source;
            event.action                  = "reject";
            event.reason                  = reason.empty() ? name : reason;
            const std::string eventReason = event.reason;
            emitChange(std::move(event));
            return rejected<int>(eve::DiagnosticCode::PreconditionViolation,
                                 "status application condition rejected the effect: " + eventReason);
        }
    }

    if (definition->durationPolicy == "instant") {
        auto candidateAttributes = actor->attributes()->values;
        for (const auto &modifier : definition->modifiers) {
            const double next = candidateAttributes.getBase(modifier.attribute) + modifier.value;
            if (!std::isfinite(next))
                return rejected<int>(eve::DiagnosticCode::InvalidArgument,
                                     "instant effect modifier overflowed the base attribute");
            candidateAttributes.modifyBase(modifier.attribute, modifier.value);
        }
        actor->attributes()->values = std::move(candidateAttributes);

        StatusChangeEvent event;
        event.actor    = actor;
        event.effectId = effectId;
        event.source   = source;
        event.action   = "apply";
        emitChange(std::move(event));
        return eve::Result<int>::success(0, eve::Status::success(eve::StatusCode::Applied));
    }

    auto               statuses = actor->statuses();
    LifecycleInstance *existing = findByType(*statuses, effectId);
    if (existing) {
        const std::string canonicalId = existing->id;
        const int         legacyId    = legacyIdFor(*statuses, canonicalId);
        const auto        metadataIt  = statuses->metadata.find(canonicalId);
        if (legacyId <= 0 || metadataIt == statuses->metadata.end())
            return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                 "RPG status adapter mapping is missing for an active effect", eve::StatusCode::Failed);

        const StatusMetadata oldMetadata = metadataIt->second;
        const StatusInstance current     = project(*existing, oldMetadata, legacyId);
        const std::string   &policy      = definition->stackPolicy;

        if (!isBuiltinStackPolicy(policy)) {
            const auto custom = stackPolicies().find(policy);
            if (custom == stackPolicies().end() || !custom->second)
                return rejected<int>(eve::DiagnosticCode::InvalidArgument,
                                     "effect stack policy is not callable: " + policy);

            StatusInstance candidate    = current;
            const int      policyResult = custom->second(actor, candidate, *definition, source);
            if (policyResult < 0) {
                StatusChangeEvent event;
                event.actor      = actor;
                event.instanceId = legacyId;
                event.effectId   = effectId;
                event.source     = source;
                event.action     = "reject";
                event.stacks     = current.stacks;
                event.reason     = "stackPolicy:" + policy;
                emitChange(std::move(event));
                return rejected<int>(eve::DiagnosticCode::Conflict,
                                     "custom effect stack policy rejected the application", eve::StatusCode::Conflict);
            }
            if (policyResult == 0) return eve::Result<int>::success(0, eve::Status::success(eve::StatusCode::NoOp));

            auto candidateValidation = validateCandidate(candidate, current, *definition);
            if (!candidateValidation.ok()) return eve::Result<int>::failure(candidateValidation.status());
            if (policyResult != legacyId)
                return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                     "custom effect stack policy returned a foreign status id",
                                     eve::StatusCode::Failed);

            const double candidateDuration = candidate.remaining < 0.0f ? 0.0 : candidate.remaining;
            auto         lifecycle =
                makeLifecycleDefinition(*definition, ::eve::effects::StackMode::Reuse,
                                        ::eve::effects::StackCountPolicy::Set, ::eve::effects::DurationPolicy::Replace,
                                        static_cast<std::uint32_t>(candidate.stacks), candidateDuration);
            // A registered custom policy owns its own stack limit semantics.
            // The generic container still supplies atomic instance replacement,
            // but must not impose the legacy RPG default maxStacks of one.
            lifecycle.policy.maxStacks = 0;
            auto candidateContainer    = statuses->container;
            auto applied               = candidateContainer.apply(lifecycle, "actor", source);
            if (!applied.ok()) return eve::Result<int>::failure(applied.status());
            const std::string updatedId = std::move(applied).takeValue();
            if (updatedId != canonicalId)
                return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                     "effect container changed an existing status identity", eve::StatusCode::Failed);
            LifecycleInstance *updated = candidateContainer.find(canonicalId);
            if (!updated)
                return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                     "effect container lost an updated status instance", eve::StatusCode::Failed);
            updated->source         = candidate.source;
            const int updatedStacks = candidate.stacks;

            auto candidateAttributes = actor->attributes()->values;
            auto bindings            = prepareModifierBindings(candidateAttributes, *definition, *updated, oldMetadata);
            if (!bindings.ok()) return eve::Result<int>::failure(bindings.status());

            auto  candidateMetadata      = statuses->metadata;
            auto &newMetadata            = candidateMetadata[canonicalId];
            newMetadata.periodAccum      = candidate.periodAccum;
            newMetadata.props            = candidate.props;
            newMetadata.appliedModifiers = std::move(bindings).takeValue();

            auto candidateLegacyIdByEffect = statuses->legacyIdByEffect;
            auto candidateEffectByLegacyId = statuses->effectByLegacyId;
            commit(actor, std::move(candidateContainer), std::move(candidateMetadata),
                   std::move(candidateLegacyIdByEffect), std::move(candidateEffectByLegacyId), statuses->nextInstanceId,
                   std::move(candidateAttributes));

            StatusChangeEvent event;
            event.actor      = actor;
            event.instanceId = legacyId;
            event.effectId   = effectId;
            event.source     = source;
            event.action     = "stack";
            event.stacks     = updatedStacks;
            emitChange(std::move(event));
            return eve::Result<int>::success(legacyId, eve::Status::success(eve::StatusCode::Applied));
        }

        if (policy == "none") {
            StatusChangeEvent event;
            event.actor      = actor;
            event.instanceId = legacyId;
            event.effectId   = effectId;
            event.source     = source;
            event.action     = "reject";
            event.stacks     = current.stacks;
            event.reason     = "stackPolicy:none";
            emitChange(std::move(event));
            return rejected<int>(eve::DiagnosticCode::Conflict, "effect is already active and uses stackPolicy:none",
                                 eve::StatusCode::Conflict);
        }

        ::eve::effects::StackCountPolicy stackCount     = ::eve::effects::StackCountPolicy::Keep;
        ::eve::effects::DurationPolicy   durationPolicy = ::eve::effects::DurationPolicy::Replace;
        ::eve::effects::OverflowPolicy   overflow       = ::eve::effects::OverflowPolicy::Reject;
        std::string                      action         = policy;
        if (policy == "refresh") {
            durationPolicy = ::eve::effects::DurationPolicy::Replace;
        } else if (policy == "extend") {
            durationPolicy = ::eve::effects::DurationPolicy::Extend;
        } else if (policy == "stack") {
            stackCount = ::eve::effects::StackCountPolicy::Increment;
            overflow   = ::eve::effects::OverflowPolicy::Clamp;
        } else {
            return rejected<int>(eve::DiagnosticCode::InvalidArgument,
                                 "unknown built-in effect stack policy: " + policy);
        }

        auto lifecycle =
            makeLifecycleDefinition(*definition, ::eve::effects::StackMode::Reuse, stackCount, durationPolicy);
        lifecycle.policy.overflow = overflow;
        auto candidateContainer   = statuses->container;
        auto applied              = candidateContainer.apply(lifecycle, "actor", source);
        if (!applied.ok()) return eve::Result<int>::failure(applied.status());
        const std::string updatedId = std::move(applied).takeValue();
        if (updatedId != canonicalId)
            return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                 "effect container changed an existing status identity", eve::StatusCode::Failed);
        LifecycleInstance *updated = candidateContainer.find(canonicalId);
        if (!updated)
            return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                                 "effect container lost an updated status instance", eve::StatusCode::Failed);

        auto candidateAttributes = actor->attributes()->values;
        auto bindings            = prepareModifierBindings(candidateAttributes, *definition, *updated, oldMetadata);
        if (!bindings.ok()) return eve::Result<int>::failure(bindings.status());
        const int updatedStacks     = updated->stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                          ? std::numeric_limits<int>::max()
                                          : static_cast<int>(updated->stackCount);
        auto      candidateMetadata = statuses->metadata;
        candidateMetadata[canonicalId].appliedModifiers = std::move(bindings).takeValue();
        auto candidateLegacyIdByEffect                  = statuses->legacyIdByEffect;
        auto candidateEffectByLegacyId                  = statuses->effectByLegacyId;
        commit(actor, std::move(candidateContainer), std::move(candidateMetadata), std::move(candidateLegacyIdByEffect),
               std::move(candidateEffectByLegacyId), statuses->nextInstanceId, std::move(candidateAttributes));

        StatusChangeEvent event;
        event.actor      = actor;
        event.instanceId = legacyId;
        event.effectId   = effectId;
        event.source     = source;
        event.action     = action;
        event.stacks     = updatedStacks;
        emitChange(std::move(event));
        return eve::Result<int>::success(legacyId, eve::Status::success(eve::StatusCode::Applied));
    }

    auto       candidateContainer = statuses->container;
    const auto lifecycle          = makeNewLifecycleDefinition(*definition);
    auto       applied            = candidateContainer.apply(lifecycle, "actor", source);
    if (!applied.ok()) return eve::Result<int>::failure(applied.status());
    const std::string  canonicalId = std::move(applied).takeValue();
    LifecycleInstance *created     = candidateContainer.find(canonicalId);
    if (!created)
        return rejected<int>(eve::DiagnosticCode::InvariantViolation,
                             "effect container did not retain a newly applied status", eve::StatusCode::Failed);

    auto               candidateMetadata         = statuses->metadata;
    auto               candidateLegacyIdByEffect = statuses->legacyIdByEffect;
    auto               candidateEffectByLegacyId = statuses->effectByLegacyId;
    int                candidateNextInstanceId   = statuses->nextInstanceId;
    RPGActor::Statuses mappingState;
    mappingState.nextInstanceId   = candidateNextInstanceId;
    mappingState.effectByLegacyId = candidateEffectByLegacyId;
    auto legacy                   = allocateLegacyId(mappingState);
    if (!legacy.ok()) return eve::Result<int>::failure(legacy.status());
    const int legacyId                     = std::move(legacy).takeValue();
    candidateNextInstanceId                = mappingState.nextInstanceId;
    candidateLegacyIdByEffect[canonicalId] = legacyId;
    candidateEffectByLegacyId[legacyId]    = canonicalId;

    auto           candidateAttributes = actor->attributes()->values;
    StatusMetadata emptyMetadata;
    auto           bindings = prepareModifierBindings(candidateAttributes, *definition, *created, emptyMetadata);
    if (!bindings.ok()) return eve::Result<int>::failure(bindings.status());
    const int createdStacks = created->stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                  ? std::numeric_limits<int>::max()
                                  : static_cast<int>(created->stackCount);
    candidateMetadata[canonicalId].appliedModifiers = std::move(bindings).takeValue();

    commit(actor, std::move(candidateContainer), std::move(candidateMetadata), std::move(candidateLegacyIdByEffect),
           std::move(candidateEffectByLegacyId), candidateNextInstanceId, std::move(candidateAttributes));

    StatusChangeEvent event;
    event.actor      = actor;
    event.instanceId = legacyId;
    event.effectId   = effectId;
    event.source     = source;
    event.action     = "apply";
    event.stacks     = createdStacks;
    emitChange(std::move(event));
    return eve::Result<int>::success(legacyId, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> StatusSystem::remove(RPGActor *actor, int instanceId) {
    if (!actor) return rejected<void>(eve::DiagnosticCode::InvalidArgument, "status removal requires an actor");
    auto statuses = actor->statuses();
    auto  mapping  = statuses->effectByLegacyId.find(instanceId);
    if (mapping == statuses->effectByLegacyId.end())
        return rejected<void>(eve::DiagnosticCode::NotFound, "status legacy instance id was not found",
                              eve::StatusCode::NotFound);
    const std::string        canonicalId = mapping->second;
    const LifecycleInstance *effect      = statuses->container.find(canonicalId);
    const auto               metadataIt  = statuses->metadata.find(canonicalId);
    if (!effect || metadataIt == statuses->metadata.end())
        return rejected<void>(eve::DiagnosticCode::InvariantViolation,
                              "RPG status adapter mapping points to stale state", eve::StatusCode::Failed);
    const StatusInstance old = project(*effect, metadataIt->second, instanceId);

    auto candidateContainer = statuses->container;
    auto removed            = candidateContainer.remove(canonicalId);
    if (!removed.ok()) return eve::Result<void>::failure(removed.status());
    auto candidateAttributes = actor->attributes()->values;
    auto bindingsRemoved     = removeBindings(candidateAttributes, metadataIt->second);
    if (!bindingsRemoved.ok()) return eve::Result<void>::failure(bindingsRemoved.status());

    auto candidateMetadata         = statuses->metadata;
    auto candidateLegacyIdByEffect = statuses->legacyIdByEffect;
    auto candidateEffectByLegacyId = statuses->effectByLegacyId;
    candidateMetadata.erase(canonicalId);
    candidateLegacyIdByEffect.erase(canonicalId);
    candidateEffectByLegacyId.erase(instanceId);
    commit(actor, std::move(candidateContainer), std::move(candidateMetadata), std::move(candidateLegacyIdByEffect),
           std::move(candidateEffectByLegacyId), statuses->nextInstanceId, std::move(candidateAttributes));

    StatusChangeEvent event;
    event.actor      = actor;
    event.instanceId = old.instanceId;
    event.effectId   = old.effectId;
    event.source     = old.source;
    event.action     = "remove";
    event.stacks     = old.stacks;
    emitChange(std::move(event));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::vector<std::string> matchingIds(const RPGActor::Statuses                             &statuses,
                                     const std::function<bool(const LifecycleInstance &)> &predicate) {
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(statuses.container.effectCount()));
    for (int i = 0; i < statuses.container.effectCount(); ++i) {
        const LifecycleInstance *effect = statuses.container.effectAt(i);
        if (effect && predicate(*effect)) result.push_back(effect->id);
    }
    return result;
}

int removeMatching(RPGActor *actor, const std::vector<std::string> &canonicalIds) {
    int removed = 0;
    for (const auto &canonicalId : canonicalIds) {
        if (!actor) continue;
        auto statuses = actor->statuses();
        const auto mapping = statuses->legacyIdByEffect.find(canonicalId);
        if (mapping == statuses->legacyIdByEffect.end()) continue;
        auto didRemove = StatusSystem::remove(actor, mapping->second);
        if (didRemove.ok()) ++removed;
    }
    return removed;
}

int StatusSystem::removeByEffect(RPGActor *actor, const std::string &effectId) {
    if (!actor) return 0;
    const auto ids = matchingIds(*actor->statuses(),
                                 [&effectId](const LifecycleInstance &effect) { return effect.type == effectId; });
    return removeMatching(actor, ids);
}

int StatusSystem::removeBySource(RPGActor *actor, const std::string &source) {
    if (!actor) return 0;
    const auto ids =
        matchingIds(*actor->statuses(), [&source](const LifecycleInstance &effect) { return effect.source == source; });
    return removeMatching(actor, ids);
}

int StatusSystem::removeByTag(RPGActor *actor, const std::string &tag) {
    if (!actor) return 0;
    const auto ids =
        matchingIds(*actor->statuses(), [&tag](const LifecycleInstance &effect) { return effect.hasTag(tag); });
    return removeMatching(actor, ids);
}

bool StatusSystem::hasEffect(RPGActor *actor, const std::string &effectId) {
    return actor && findByType(*actor->statuses(), effectId) != nullptr;
}

bool StatusSystem::hasTag(RPGActor *actor, const std::string &tag) {
    if (!actor) return false;
    for (int i = 0; i < actor->statuses()->container.effectCount(); ++i) {
        const LifecycleInstance *effect = actor->statuses()->container.effectAt(i);
        if (effect && effect->hasTag(tag)) return true;
    }
    return false;
}

int StatusSystem::getActiveCount(RPGActor *actor) { return actor ? actor->statuses()->container.effectCount() : 0; }

std::string StatusSystem::getActiveEffectId(RPGActor *actor, int index) {
    if (!actor) return {};
    const LifecycleInstance *effect = actor->statuses()->container.effectAt(index);
    return effect ? effect->type : std::string{};
}

int StatusSystem::getActiveStacks(RPGActor *actor, int index) {
    if (!actor) return 0;
    const LifecycleInstance *effect = actor->statuses()->container.effectAt(index);
    if (!effect) return 0;
    return effect->stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(effect->stackCount);
}

float StatusSystem::getActiveRemaining(RPGActor *actor, int index) {
    if (!actor) return 0.0f;
    const LifecycleInstance *effect = actor->statuses()->container.effectAt(index);
    return effect ? static_cast<float>(effect->remaining) : 0.0f;
}

int StatusSystem::getActiveInstanceId(RPGActor *actor, int index) {
    if (!actor) return 0;
    const LifecycleInstance *effect = actor->statuses()->container.effectAt(index);
    return effect ? legacyIdFor(*actor->statuses(), effect->id) : 0;
}

std::string StatusSystem::getActiveSource(RPGActor *actor, int index) {
    if (!actor) return {};
    const LifecycleInstance *effect = actor->statuses()->container.effectAt(index);
    return effect ? effect->source : std::string{};
}

std::string StatusSystem::getProp(RPGActor *actor, int instanceId, const std::string &key,
                                  const std::string &fallback) {
    if (!actor) return fallback;
    const LifecycleInstance *effect = findByLegacyId(*actor->statuses(), instanceId);
    if (!effect) return fallback;
    const auto metadata = actor->statuses()->metadata.find(effect->id);
    if (metadata == actor->statuses()->metadata.end()) return fallback;
    const auto property = metadata->second.props.find(key);
    return property == metadata->second.props.end() ? fallback : property->second;
}

bool StatusSystem::setProp(RPGActor *actor, int instanceId, const std::string &key, const std::string &value) {
    if (!actor) return false;
    LifecycleInstance *effect = findByLegacyId(*actor->statuses(), instanceId);
    if (!effect) return false;
    auto metadata = actor->statuses()->metadata.find(effect->id);
    if (metadata == actor->statuses()->metadata.end()) return false;
    metadata->second.props[key] = value;
    return true;
}

eve::Result<StatusUpdateSummary> StatusSystem::update(double dt) {
    if (!std::isfinite(dt) || dt < 0.0)
        return rejected<StatusUpdateSummary>(eve::DiagnosticCode::InvalidArgument,
                                             "status update delta must be finite and non-negative");

    StatusUpdateSummary total;
    total.elapsedSeconds = dt;

    for (RPGActor *actor : RPGActor::liveActors()) {
        if (!actor) continue;
        auto                     statuses = actor->statuses();
        std::vector<std::string> activeIds;
        activeIds.reserve(static_cast<std::size_t>(statuses->container.effectCount()));
        for (int i = 0; i < statuses->container.effectCount(); ++i) {
            const LifecycleInstance *effect = statuses->container.effectAt(i);
            if (effect) activeIds.push_back(effect->id);
        }

        auto                           candidateContainer        = statuses->container;
        auto                           candidateMetadata         = statuses->metadata;
        auto                           candidateLegacyIdByEffect = statuses->legacyIdByEffect;
        auto                           candidateEffectByLegacyId = statuses->effectByLegacyId;
        auto                           candidateAttributes       = actor->attributes()->values;
        std::vector<StatusTickEvent>   pendingTicks;
        std::vector<StatusChangeEvent> pendingChanges;

        for (const auto &canonicalId : activeIds) {
            const LifecycleInstance *effect     = statuses->container.find(canonicalId);
            auto                     metadataIt = candidateMetadata.find(canonicalId);
            if (!effect || metadataIt == candidateMetadata.end())
                return rejected<StatusUpdateSummary>(eve::DiagnosticCode::InvariantViolation,
                                                     "RPG status executor metadata is out of sync",
                                                     eve::StatusCode::Failed);
            const EffectDefinition *definition = EffectRegistry::find(effect->type);
            if (!definition || definition->period <= 0.0f) continue;

            double available = dt;
            if (effect->remaining >= 0.0) available = std::min(available, effect->remaining);
            double       accumulated = metadataIt->second.periodAccum + available;
            const double period      = static_cast<double>(definition->period);
            const int    legacyId    = legacyIdFor(*statuses, canonicalId);
            if (legacyId <= 0)
                return rejected<StatusUpdateSummary>(eve::DiagnosticCode::InvariantViolation,
                                                     "RPG status executor has no legacy id mapping",
                                                     eve::StatusCode::Failed);
            while (accumulated >= period) {
                accumulated -= period;
                StatusTickEvent tick;
                tick.actor      = actor;
                tick.instanceId = legacyId;
                tick.effectId   = effect->type;
                tick.source     = effect->source;
                tick.stacks     = effect->stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                      ? std::numeric_limits<int>::max()
                                      : static_cast<int>(effect->stackCount);
                pendingTicks.push_back(std::move(tick));
            }
            metadataIt->second.periodAccum = accumulated;
        }

        auto advanced = candidateContainer.update(dt);
        if (!advanced.ok()) return eve::Result<StatusUpdateSummary>::failure(advanced.status());
        const auto updateSummary = std::move(advanced).takeValue();

        for (const auto &canonicalId : activeIds) {
            if (candidateContainer.find(canonicalId)) continue;
            const LifecycleInstance *oldEffect   = statuses->container.find(canonicalId);
            const auto               oldMetadata = statuses->metadata.find(canonicalId);
            const auto               mapping     = statuses->legacyIdByEffect.find(canonicalId);
            if (!oldEffect || oldMetadata == statuses->metadata.end() || mapping == statuses->legacyIdByEffect.end())
                return rejected<StatusUpdateSummary>(eve::DiagnosticCode::InvariantViolation,
                                                     "expired RPG status has incomplete adapter metadata",
                                                     eve::StatusCode::Failed);

            auto bindingsRemoved = removeBindings(candidateAttributes, oldMetadata->second);
            if (!bindingsRemoved.ok()) return eve::Result<StatusUpdateSummary>::failure(bindingsRemoved.status());

            StatusChangeEvent expired;
            expired.actor      = actor;
            expired.instanceId = mapping->second;
            expired.effectId   = oldEffect->type;
            expired.source     = oldEffect->source;
            expired.action     = "expire";
            expired.stacks     = oldEffect->stackCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
                                     ? std::numeric_limits<int>::max()
                                     : static_cast<int>(oldEffect->stackCount);
            pendingChanges.push_back(std::move(expired));
            candidateMetadata.erase(canonicalId);
            candidateLegacyIdByEffect.erase(canonicalId);
            candidateEffectByLegacyId.erase(mapping->second);
        }

        commit(actor, std::move(candidateContainer), std::move(candidateMetadata), std::move(candidateLegacyIdByEffect),
               std::move(candidateEffectByLegacyId), statuses->nextInstanceId, std::move(candidateAttributes));
        for (auto &tick : pendingTicks) {
            tickQueue().push_back(std::move(tick));
            ++total.ticks;
        }
        for (auto &change : pendingChanges) {
            emitChange(std::move(change));
            ++total.expired;
        }
        (void)updateSummary;
    }
    return eve::Result<StatusUpdateSummary>::success(std::move(total), eve::Status::success(eve::StatusCode::Applied));
}

void StatusSystem::pollTicks(std::vector<StatusTickEvent> &out) {
    auto &queue = tickQueue();
    for (auto &event : queue) out.push_back(std::move(event));
    queue.clear();
}

void StatusSystem::pollChanges(std::vector<StatusChangeEvent> &out) {
    auto &queue = changeQueue();
    for (auto &event : queue) out.push_back(std::move(event));
    queue.clear();
}

}  // namespace eve::rpg
