#include "effects/EffectDefinitionRuntime.h"

#include "common/Value.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace eve::effects {
namespace {

template <class T>
eve::Result<T> invalid(std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                                          std::move(path), {}, "effects.definition_runtime"));
}

eve::Result<eve::definition::DefinitionHandle> currentHandle(eve::definitions::DefinitionRegistry& registry,
                                                             const eve::DefinitionRef&             reference) {
    const auto& logical = reference.id();
    return registry.handle(std::string(logical.namespaceName()), std::string(logical.name()));
}

const eve::Value* field(const eve::Value::Object& object, const char* name) {
    const auto it = object.find(name);
    return it == object.end() ? nullptr : &it->second;
}

bool readString(const eve::Value::Object& object, const char* name, std::string& output) {
    const auto* value = field(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    output = *text;
    return true;
}

bool readInt(const eve::Value::Object& object, const char* name, int& output) {
    const auto* value = field(object, name);
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        if (*integer < std::numeric_limits<int>::min() || *integer > std::numeric_limits<int>::max()) return false;
        output = static_cast<int>(*integer);
        return true;
    }
    return false;
}

bool readUInt(const eve::Value::Object& object, const char* name, std::uint32_t& output) {
    const auto* value   = field(object, name);
    const auto* integer = value ? value->getIf<std::int64_t>() : nullptr;
    if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) > std::numeric_limits<std::uint32_t>::max())
        return false;
    output = static_cast<std::uint32_t>(*integer);
    return true;
}

bool readDouble(const eve::Value::Object& object, const char* name, double& output) {
    const auto* value = field(object, name);
    if (const auto* number = value ? value->getIf<double>() : nullptr) {
        output = *number;
        return std::isfinite(output);
    }
    if (const auto* integer = value ? value->getIf<std::int64_t>() : nullptr) {
        output = static_cast<double>(*integer);
        return std::isfinite(output);
    }
    return false;
}

template <class Enum>
bool readEnum(const eve::Value::Object& object, const char* name, const char* const* names, const Enum* values,
              std::size_t count, Enum& output) {
    const auto* value = field(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (*text == names[i]) {
            output = values[i];
            return true;
        }
    }
    return false;
}

template <class Enum>
bool readOptionalEnum(const eve::Value::Object& object, const char* name, const char* const* names, const Enum* values,
                      std::size_t count, Enum& output) {
    const auto* value = field(object, name);
    if (value == nullptr) return true;
    return readEnum(object, name, names, values, count, output);
}

eve::Result<EffectRuntimeState> parseState(const eve::definitions::Definition& definition,
                                           const eve::DefinitionRef&           reference) {
    auto parsed = eve::Value::fromJson(definition.json);
    if (!parsed) return eve::Result<EffectRuntimeState>::failure(parsed.status());
    auto        value  = std::move(parsed).takeValue();
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return invalid<EffectRuntimeState>("effect definition payload must be an object", "json");

    EffectRuntimeState state;
    state.stackKey = std::string(reference.id().name());
    if (const auto* value = field(*object, "stackKey")) {
        const auto* text = value->getIf<std::string>();
        if (!text) return invalid<EffectRuntimeState>("effect stackKey must be a string", "stackKey");
        state.stackKey = *text;
    }
    if (const auto* value = field(*object, "priority")) {
        if (!readInt(*object, "priority", state.priority))
            return invalid<EffectRuntimeState>("effect priority must be Int64", "priority");
    }
    if (const auto* value = field(*object, "duration")) {
        (void)value;
        if (!readDouble(*object, "duration", state.duration))
            return invalid<EffectRuntimeState>("effect duration must be finite", "duration");
    }
    if (const auto* value = field(*object, "magnitude")) {
        (void)value;
        if (!readDouble(*object, "magnitude", state.magnitude))
            return invalid<EffectRuntimeState>("effect magnitude must be finite", "magnitude");
    }
    if (const auto* value = field(*object, "stackCount")) {
        (void)value;
        if (!readUInt(*object, "stackCount", state.stackCount) || state.stackCount == 0)
            return invalid<EffectRuntimeState>("effect stackCount must be positive", "stackCount");
    }
    if (const auto* value = field(*object, "maxStacks")) {
        (void)value;
        if (!readUInt(*object, "maxStacks", state.maxStacks))
            return invalid<EffectRuntimeState>("effect maxStacks must be UInt32", "maxStacks");
    }
    if (state.maxStacks != 0 && state.stackCount > state.maxStacks)
        return invalid<EffectRuntimeState>("effect stackCount exceeds maxStacks", "stackCount");
    constexpr const char* stackModeNames[]  = {"replace", "new_instance", "reuse", "accumulate"};
    constexpr StackMode   stackModeValues[] = {StackMode::Replace, StackMode::NewInstance, StackMode::Reuse,
                                               StackMode::Accumulate};
    if (!readOptionalEnum(*object, "stackMode", stackModeNames, stackModeValues, std::size(stackModeNames),
                          state.policy.stackMode))
        return invalid<EffectRuntimeState>("effect stackMode is invalid", "stackMode");
    constexpr const char*      stackCountPolicyNames[]  = {"keep", "increment", "set"};
    constexpr StackCountPolicy stackCountPolicyValues[] = {StackCountPolicy::Keep, StackCountPolicy::Increment,
                                                           StackCountPolicy::Set};
    if (!readOptionalEnum(*object, "stackCountPolicy", stackCountPolicyNames, stackCountPolicyValues,
                          std::size(stackCountPolicyNames), state.policy.stackCount))
        return invalid<EffectRuntimeState>("effect stackCountPolicy is invalid", "stackCountPolicy");
    constexpr const char*    durationPolicyNames[]  = {"keep", "replace", "extend"};
    constexpr DurationPolicy durationPolicyValues[] = {DurationPolicy::Keep, DurationPolicy::Replace,
                                                       DurationPolicy::Extend};
    if (!readOptionalEnum(*object, "durationPolicy", durationPolicyNames, durationPolicyValues,
                          std::size(durationPolicyNames), state.policy.duration))
        return invalid<EffectRuntimeState>("effect durationPolicy is invalid", "durationPolicy");
    constexpr const char*     magnitudePolicyNames[]  = {"keep", "replace", "add", "max"};
    constexpr MagnitudePolicy magnitudePolicyValues[] = {MagnitudePolicy::Keep, MagnitudePolicy::Replace,
                                                         MagnitudePolicy::Add, MagnitudePolicy::Max};
    if (!readOptionalEnum(*object, "magnitudePolicy", magnitudePolicyNames, magnitudePolicyValues,
                          std::size(magnitudePolicyNames), state.policy.magnitude))
        return invalid<EffectRuntimeState>("effect magnitudePolicy is invalid", "magnitudePolicy");
    constexpr const char*    overflowPolicyNames[]  = {"reject", "clamp", "replace_oldest"};
    constexpr OverflowPolicy overflowPolicyValues[] = {OverflowPolicy::Reject, OverflowPolicy::Clamp,
                                                       OverflowPolicy::ReplaceOldest};
    if (!readOptionalEnum(*object, "overflowPolicy", overflowPolicyNames, overflowPolicyValues,
                          std::size(overflowPolicyNames), state.policy.overflow))
        return invalid<EffectRuntimeState>("effect overflowPolicy is invalid", "overflowPolicy");
    state.policy.maxStacks = state.maxStacks;
    state.remaining        = state.duration > 0.0 ? state.duration : -1.0;

    if (const auto* value = field(*object, "tags")) {
        const auto* array = value->getIf<eve::Value::Array>();
        if (!array) return invalid<EffectRuntimeState>("effect tags must be an array", "tags");
        for (const auto& item : *array) {
            const auto* tag = item.getIf<std::string>();
            if (!tag || tag->empty())
                return invalid<EffectRuntimeState>("effect tag must be a non-empty string", "tags");
            state.tags.push_back(*tag);
        }
        std::sort(state.tags.begin(), state.tags.end());
        state.tags.erase(std::unique(state.tags.begin(), state.tags.end()), state.tags.end());
    }
    if (const auto* value = field(*object, "payload")) {
        const auto* payload = value->getIf<eve::Value::Object>();
        if (!payload) return invalid<EffectRuntimeState>("effect payload must be an object", "payload");
        for (const auto& [key, item] : *payload) {
            auto json = item.toJson();
            if (!json) return eve::Result<EffectRuntimeState>::failure(json.status());
            auto stored = state.payload.setJson(key, std::move(json).takeValue());
            if (!stored.ok())
                return invalid<EffectRuntimeState>("effect payload member could not be stored", "payload." + key);
        }
    }
    return eve::Result<EffectRuntimeState>::success(std::move(state));
}

eve::Result<EffectRuntimeState> resolveState(eve::definitions::DefinitionRegistry&    registry,
                                             const eve::DefinitionRef&                reference,
                                             const eve::definition::DefinitionHandle& handle) {
    auto definition = registry.resolveHandle(handle);
    if (!definition) return eve::Result<EffectRuntimeState>::failure(definition.status());
    return parseState(definition.value().get(), reference);
}

}  // namespace

eve::Result<EffectDefinitionRuntime> EffectDefinitionRuntime::create(eve::definitions::DefinitionRegistry& registry,
                                                                     eve::DefinitionRef definition, std::string subject,
                                                                     std::string source, eve::PersistentId instanceId,
                                                                     eve::definition::ReloadPolicy policy) {
    if (!definition.id().isValid()) return invalid<EffectDefinitionRuntime>("effect definition reference is invalid");
    if (subject.empty()) return invalid<EffectDefinitionRuntime>("effect subject must not be empty", "subject");
    auto handle = currentHandle(registry, definition);
    if (!handle) return eve::Result<EffectDefinitionRuntime>::failure(handle.status());
    auto state = resolveState(registry, definition, handle.value());
    if (!state) return eve::Result<EffectDefinitionRuntime>::failure(state.status());
    auto runtime = eve::definition::RuntimeInstance<EffectRuntimeState>::create(
        instanceId, std::move(definition), handle.value().generation, std::move(state).takeValue());
    if (!runtime) return eve::Result<EffectDefinitionRuntime>::failure(runtime.status());
    return eve::Result<EffectDefinitionRuntime>::success(EffectDefinitionRuntime(
        registry, std::move(runtime).takeValue(), std::move(subject), std::move(source), policy));
}

const eve::definition::InstanceIdentity& EffectDefinitionRuntime::identity() const noexcept {
    return runtime_.identity();
}

const EffectRuntimeState& EffectDefinitionRuntime::state() const noexcept { return runtime_.state(); }

EffectRuntimeState& EffectDefinitionRuntime::state() noexcept { return runtime_.state(); }

eve::definition::DefinitionHandle EffectDefinitionRuntime::definitionHandle() const noexcept {
    return runtime_.identity().definitionHandle();
}

void EffectDefinitionRuntime::setActive(bool active) noexcept { runtime_.setActive(active); }

bool EffectDefinitionRuntime::isActive() const noexcept { return runtime_.isActive(); }

eve::Result<void> EffectDefinitionRuntime::applyTo(EffectInstance* effect) const {
    if (effect == nullptr)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "effect runtime cannot project to a null instance",
                                                                 "effect", {}, "effects.definition_runtime"));
    EffectInstance candidate     = *effect;
    candidate.id                 = identity().instanceId.format();
    candidate.subject            = subject_;
    candidate.type               = identity().definition.id().name();
    candidate.source             = source_;
    candidate.stackKey           = state().stackKey;
    candidate.priority           = state().priority;
    candidate.duration           = state().duration;
    candidate.remaining          = state().remaining;
    candidate.magnitude          = state().magnitude;
    candidate.stackCount         = state().stackCount;
    candidate.policy             = state().policy;
    candidate.payload            = state().payload;
    candidate.tags               = state().tags;
    candidate.identity           = eve::EffectId::fromUuid(identity().instanceId);
    candidate.definitionIdentity = identity();
    candidate.reloadPolicy       = policy_;
    using std::swap;
    swap(*effect, candidate);
    return eve::Result<void>::success();
}

eve::Result<eve::definition::ReloadOutcome> EffectDefinitionRuntime::reload(eve::definition::ReloadPolicy policy) {
    if (registry_ == nullptr)
        return invalid<eve::definition::ReloadOutcome>("effect definition registry is not bound", "registry");
    auto next = currentHandle(*registry_, identity().definition);
    if (!next) return eve::Result<eve::definition::ReloadOutcome>::failure(next.status());
    auto defaults = resolveState(*registry_, identity().definition, next.value());
    if (!defaults) return eve::Result<eve::definition::ReloadOutcome>::failure(defaults.status());
    EffectRuntimeState defaultState = std::move(defaults).takeValue();
    auto rebuild = [defaultState](const EffectRuntimeState& oldState, const eve::definition::InstanceIdentity&,
                                  const eve::definition::DefinitionHandle&) mutable -> eve::Result<EffectRuntimeState> {
        EffectRuntimeState rebuilt = defaultState;
        if (oldState.remaining >= 0.0)
            rebuilt.remaining = rebuilt.duration > 0.0 ? std::min(oldState.remaining, rebuilt.duration) : -1.0;
        rebuilt.stackCount =
            rebuilt.maxStacks == 0 ? oldState.stackCount : std::min(oldState.stackCount, rebuilt.maxStacks);
        if (rebuilt.stackCount == 0) rebuilt.stackCount = 1;
        return eve::Result<EffectRuntimeState>::success(std::move(rebuilt));
    };
    auto result = runtime_.reload(next.value(), policy, defaultState, std::move(rebuild));
    if (result.ok()) policy_ = policy;
    return result;
}

}  // namespace eve::effects
