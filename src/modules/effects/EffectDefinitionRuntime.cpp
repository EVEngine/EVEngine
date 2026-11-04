#include "effects/EffectDefinitionRuntime.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>
#include "common/Value.h"
#include "common/ValueRead.h"
#include "effects/EffectTypes.h"

// EffectDefinitionRuntime.cpp
namespace eve::effects {
namespace {

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
    auto text = eve::readStringMember(object, name);
    if (!text) return false;
    output = std::move(text.value());
    return true;
}

bool readInt(const eve::Value::Object& object, const char* name, int& output) {
    const auto* value = field(object, name);
    if (!value) return false;
    auto parsed = eve::readIntValue(*value);
    if (!parsed) return false;
    output = parsed.value();
    return true;
}

bool readUInt(const eve::Value::Object& object, const char* name, std::uint32_t& output) {
    const auto* value = field(object, name);
    if (!value) return false;
    auto parsed = eve::readUInt32Value(*value);
    if (!parsed) return false;
    output = parsed.value();
    return true;
}

bool readDouble(const eve::Value::Object& object, const char* name, double& output) {
    const auto* value = field(object, name);
    if (!value) return false;
    auto parsed = eve::readNumericDoubleValue(*value);
    if (!parsed) return false;
    output = parsed.value();
    return std::isfinite(output);
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
    if (!object)
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition payload must be an object",
                                   "json", {}, "effects.definition_runtime"));

    EffectRuntimeState state;
    state.stackKey = std::string(reference.id().name());
    if (const auto* value = field(*object, "stackKey")) {
        const auto* text = value->getIf<std::string>();
        if (!text)
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stackKey must be a string",
                                       "stackKey", {}, "effects.definition_runtime"));
        state.stackKey = *text;
    }
    if (const auto* value = field(*object, "priority")) {
        if (!readInt(*object, "priority", state.priority))
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect priority must be Int64",
                                       "priority", {}, "effects.definition_runtime"));
    }
    if (const auto* value = field(*object, "duration")) {
        (void)value;
        if (!readDouble(*object, "duration", state.duration))
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect duration must be finite",
                                       "duration", {}, "effects.definition_runtime"));
    }
    if (const auto* value = field(*object, "magnitude")) {
        (void)value;
        if (!readDouble(*object, "magnitude", state.magnitude))
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect magnitude must be finite",
                                       "magnitude", {}, "effects.definition_runtime"));
    }
    if (const auto* value = field(*object, "stackCount")) {
        (void)value;
        if (!readUInt(*object, "stackCount", state.stackCount) || state.stackCount == 0)
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stackCount must be positive",
                                       "stackCount", {}, "effects.definition_runtime"));
    }
    if (const auto* value = field(*object, "maxStacks")) {
        (void)value;
        if (!readUInt(*object, "maxStacks", state.maxStacks))
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect maxStacks must be UInt32",
                                       "maxStacks", {}, "effects.definition_runtime"));
    }
    if (state.maxStacks != 0 && state.stackCount > state.maxStacks)
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stackCount exceeds maxStacks",
                                   "stackCount", {}, "effects.definition_runtime"));
    constexpr const char* stackModeNames[]  = {"replace", "new_instance", "reuse", "accumulate"};
    constexpr StackMode   stackModeValues[] = {StackMode::Replace, StackMode::NewInstance, StackMode::Reuse,
                                               StackMode::Accumulate};
    if (!readOptionalEnum(*object, "stackMode", stackModeNames, stackModeValues, std::size(stackModeNames),
                          state.policy.stackMode))
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stackMode is invalid", "stackMode", {},
                                   "effects.definition_runtime"));
    constexpr const char*      stackCountPolicyNames[]  = {"keep", "increment", "set"};
    constexpr StackCountPolicy stackCountPolicyValues[] = {StackCountPolicy::Keep, StackCountPolicy::Increment,
                                                           StackCountPolicy::Set};
    if (!readOptionalEnum(*object, "stackCountPolicy", stackCountPolicyNames, stackCountPolicyValues,
                          std::size(stackCountPolicyNames), state.policy.stackCount))
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stackCountPolicy is invalid",
                                   "stackCountPolicy", {}, "effects.definition_runtime"));
    constexpr const char*    durationPolicyNames[]  = {"keep", "replace", "extend"};
    constexpr DurationPolicy durationPolicyValues[] = {DurationPolicy::Keep, DurationPolicy::Replace,
                                                       DurationPolicy::Extend};
    if (!readOptionalEnum(*object, "durationPolicy", durationPolicyNames, durationPolicyValues,
                          std::size(durationPolicyNames), state.policy.duration))
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect durationPolicy is invalid",
                                   "durationPolicy", {}, "effects.definition_runtime"));
    constexpr const char*     magnitudePolicyNames[]  = {"keep", "replace", "add", "max"};
    constexpr MagnitudePolicy magnitudePolicyValues[] = {MagnitudePolicy::Keep, MagnitudePolicy::Replace,
                                                         MagnitudePolicy::Add, MagnitudePolicy::Max};
    if (!readOptionalEnum(*object, "magnitudePolicy", magnitudePolicyNames, magnitudePolicyValues,
                          std::size(magnitudePolicyNames), state.policy.magnitude))
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect magnitudePolicy is invalid",
                                   "magnitudePolicy", {}, "effects.definition_runtime"));
    constexpr const char*    overflowPolicyNames[]  = {"reject", "clamp", "replace_oldest"};
    constexpr OverflowPolicy overflowPolicyValues[] = {OverflowPolicy::Reject, OverflowPolicy::Clamp,
                                                       OverflowPolicy::ReplaceOldest};
    if (!readOptionalEnum(*object, "overflowPolicy", overflowPolicyNames, overflowPolicyValues,
                          std::size(overflowPolicyNames), state.policy.overflow))
        return eve::Result<EffectRuntimeState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect overflowPolicy is invalid",
                                   "overflowPolicy", {}, "effects.definition_runtime"));
    state.policy.maxStacks = state.maxStacks;
    state.remaining        = state.duration > 0.0 ? state.duration : -1.0;

    if (const auto* value = field(*object, "tags")) {
        const auto* array = value->getIf<eve::Value::Array>();
        if (!array)
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect tags must be an array", "tags", {},
                                       "effects.definition_runtime"));
        for (const auto& item : *array) {
            const auto* tag = item.getIf<std::string>();
            if (!tag || tag->empty())
                return eve::Result<EffectRuntimeState>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "effect tag must be a non-empty string", "tags", {},
                    "effects.definition_runtime"));
            state.tags.push_back(*tag);
        }
        std::sort(state.tags.begin(), state.tags.end());
        state.tags.erase(std::unique(state.tags.begin(), state.tags.end()), state.tags.end());
    }
    if (const auto* value = field(*object, "payload")) {
        const auto* payload = value->getIf<eve::Value::Object>();
        if (!payload)
            return eve::Result<EffectRuntimeState>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect payload must be an object",
                                       "payload", {}, "effects.definition_runtime"));
        for (const auto& [key, item] : *payload) {
            auto json = item.toJson();
            if (!json) return eve::Result<EffectRuntimeState>::failure(json.status());
            auto stored = state.payload.setJson(key, std::move(json).takeValue());
            if (!stored.ok())
                return eve::Result<EffectRuntimeState>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "effect payload member could not be stored", "payload." + key,
                    {}, "effects.definition_runtime"));
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
    if (!definition.id().isValid())
        return eve::Result<EffectDefinitionRuntime>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition reference is invalid", {},
                                   {}, "effects.definition_runtime"));
    if (subject.empty())
        return eve::Result<EffectDefinitionRuntime>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect subject must not be empty", "subject",
                                   {}, "effects.definition_runtime"));
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
        return eve::Result<eve::definition::ReloadOutcome>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition registry is not bound",
                                   "registry", {}, "effects.definition_runtime"));
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

// EffectTypes.cpp
namespace eve::effects {
// namespace

void EffectPayload::setString(const std::string& key, const std::string& value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void EffectPayload::setNumber(const std::string& key, double value) {
    if (key.empty() || !std::isfinite(value)) return;
    values_[key] = eve::Value(value);
}

void EffectPayload::setBool(const std::string& key, bool value) {
    if (!key.empty()) values_[key] = eve::Value(value);
}

void EffectPayload::setNull(const std::string& key) {
    if (!key.empty()) values_[key] = eve::Value();
}

eve::Result<void> EffectPayload::setJson(const std::string& key, const std::string& json) {
    if (key.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect payload key must not be empty"));
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    values_[key] = std::move(parsed).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool EffectPayload::has(const std::string& key) const { return values_.contains(key); }

eve::Result<void> EffectPayload::erase(const std::string& key) {
    if (key.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect payload key must not be empty"));
    const bool removed = values_.erase(key) != 0;
    return eve::Result<void>::success(eve::Status::success(removed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

std::string EffectPayload::getJson(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return {};
    auto result = it->second.toJson();
    if (!result) return {};
    return std::move(result).takeValue();
}

std::string EffectPayload::toJson() const {
    auto result = eve::Value(values_).toJson();
    if (!result) return {};
    return std::move(result).takeValue();
}

const eve::Value::Object& EffectPayload::object() const noexcept { return values_; }

void EffectPayload::clear() { values_.clear(); }

eve::Result<void> EffectDefinition::validate() const {
    if (id.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition id must not be empty"));
    if (!std::isfinite(duration))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition duration must be finite"));
    if (!std::isfinite(period) || period < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "effect definition period must be finite and non-negative"));
    if (!std::isfinite(magnitude))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect definition magnitude must be finite"));
    if (stackCount == 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "effect definition stack count must be positive"));
    if (policy.stackCount == StackCountPolicy::Set && policy.maxStacks != 0 && stackCount > policy.maxStacks &&
        policy.overflow == OverflowPolicy::Reject) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "effect definition stack count exceeds its maximum"));
    }
    return eve::Result<void>::success();
}

eve::Result<void> EffectInstance::addTag(const std::string& tag) {
    if (tag.empty())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect tag must not be empty"));
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it != tags.end() && *it == tag) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    tags.insert(it, tag);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> EffectInstance::removeTag(const std::string& tag) {
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it == tags.end() || *it != tag) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    tags.erase(it);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool EffectInstance::hasTag(const std::string& tag) const { return std::binary_search(tags.begin(), tags.end(), tag); }

int EffectInstance::tagCount() const { return static_cast<int>(tags.size()); }

std::string EffectInstance::tagAt(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= tags.size() ? std::string{}
                                                                       : tags[static_cast<std::size_t>(index)];
}

std::string policyName(StackPolicy policy) {
    switch (policy) {
        case StackPolicy::Replace: return "replace";
        case StackPolicy::Stack: return "stack";
        case StackPolicy::Refresh: return "refresh";
    }
    return "unknown";
}

bool parsePolicy(const std::string& name, StackPolicy& policy) {
    if (name == "replace")
        policy = StackPolicy::Replace;
    else if (name == "stack")
        policy = StackPolicy::Stack;
    else if (name == "refresh")
        policy = StackPolicy::Refresh;
    else
        return false;
    return true;
}

std::string eventKindName(EffectEventKind kind) {
    switch (kind) {
        case EffectEventKind::Applied: return "applied";
        case EffectEventKind::Refreshed: return "refreshed";
        case EffectEventKind::Stacked: return "stacked";
        case EffectEventKind::Periodic: return "periodic";
        case EffectEventKind::Expired: return "expired";
        case EffectEventKind::Removed: return "removed";
    }
    return "unknown";
}

}  // namespace eve::effects
