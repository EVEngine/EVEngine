#include "effects/Effects.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace eve::effects {

namespace {

/** @brief Script-owned proxy; the effect container remains module-owned. */
struct ScriptEffectContainer {
    explicit ScriptEffectContainer(EffectContainerHandleRef value) : reference(value) {}
    ~ScriptEffectContainer() noexcept {
        Effects::release(reference).ignore("script effect container proxy destruction");
    }
    EffectContainerHandleRef reference;
};

template <class T>
eve::Result<T> effectsBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "effects.squirrel"));
}

eve::Result<EffectDefinition> effectDefinitionFromBinding(const std::string& type, int priority, float duration,
                                                          const std::string& stackKey,
                                                          const std::string& policyNameValue) {
    StackPolicy policy;
    if (!parsePolicy(policyNameValue, policy))
        return effectsBindingFailure<EffectDefinition>(eve::DiagnosticCode::InvalidArgument,
                                                       "unknown effect stack policy", "policy");

    EffectDefinition definition;
    definition.id       = type;
    definition.stackKey = stackKey;
    definition.priority = priority;
    definition.duration = duration;
    switch (policy) {
        case StackPolicy::Replace: definition.policy.stackMode = StackMode::Replace; break;
        case StackPolicy::Stack: definition.policy.stackMode = StackMode::NewInstance; break;
        case StackPolicy::Refresh: definition.policy.stackMode = StackMode::Reuse; break;
    }
    auto validation = definition.validate();
    if (!validation.ok()) return eve::Result<EffectDefinition>::failure(validation.status());
    return eve::Result<EffectDefinition>::success(std::move(definition));
}

eve::Value effectUpdateProjection(EffectUpdateSummary summary) {
    return eve::Value(eve::Value::Object{
        {"expired", eve::Value(static_cast<std::int64_t>(summary.expired))},
        {"elapsedSeconds", eve::Value(summary.elapsedSeconds)},
        {"tick", eve::Value(static_cast<std::int64_t>(summary.tick.value()))},
    });
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned effects proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned effects allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

}  // namespace

eve::Result<EffectContainerHandleRef> Effects::newContainer() {
    Effects* module = Effects::create();
    return module->containers_.emplace(std::make_unique<EffectContainer>());
}

eve::script::Borrowed<EffectContainer> Effects::resolve(EffectContainerHandleRef reference) noexcept {
    Effects* module = ModuleManager::getInstance<Effects>("Effects");
    if (!module) return {};
    return module->containers_.resolve(reference);
}

eve::Result<void> Effects::release(EffectContainerHandleRef reference) {
    Effects* module = ModuleManager::getInstance<Effects>("Effects");
    if (!module)
        return effectsBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Effects module is no longer loaded",
                                           "container");
    return module->containers_.erase(reference);
}

bool Effects::isStale(EffectContainerHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Effects* module = ModuleManager::getInstance<Effects>("Effects");
    return !module || module->containers_.isStale(reference);
}

Module_IMPL(Effects, new Effects());

void Effects::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto payload = table.addClass<EffectPayload>(
        "EffectPayload", std::function<EffectPayload*()>([]() -> EffectPayload* { return nullptr; }), false);
    payload.addFunc("setString", &EffectPayload::setString);
    payload.addFunc("setNumber", [](EffectPayload* value, const std::string& key, float number) {
        if (value) value->setNumber(key, number);
    });
    payload.addFunc("setBool", &EffectPayload::setBool);
    payload.addFunc("setNull", &EffectPayload::setNull);
    payload.addFunc("setJson", [vm](EffectPayload* value, const std::string& key, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "effect payload must not be null",
                                                "payload"));
        return eve::script::projectResult(vm, value->setJson(key, json));
    });
    payload.addFunc("has", &EffectPayload::has);
    payload.addFunc("erase", [vm](EffectPayload* value, const std::string& key) {
        if (!value)
            return eve::script::projectResult(
                vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument, "effect payload must not be null",
                                                "payload"));
        return eve::script::projectResult(vm, value->erase(key));
    });
    payload.addFunc("getJson", &EffectPayload::getJson);
    payload.addFunc("toJson", &EffectPayload::toJson);
    payload.addFunc("clear", &EffectPayload::clear);

    auto effect =
        table.addClass<Effect>("Effect", std::function<Effect*()>([]() -> Effect* { return nullptr; }), false);
    effect.addFunc("getId", [](Effect* value) { return value ? value->id : std::string{}; });
    effect.addFunc("getSubject", [](Effect* value) { return value ? value->subject : std::string{}; });
    effect.addFunc("getType", [](Effect* value) { return value ? value->type : std::string{}; });
    effect.addFunc("getSource", [](Effect* value) { return value ? value->source : std::string{}; });
    effect.addFunc("getStackKey", [](Effect* value) { return value ? value->stackKey : std::string{}; });
    effect.addFunc("getPriority", [](Effect* value) { return value ? value->priority : 0; });
    effect.addFunc("getDuration", [](Effect* value) { return value ? static_cast<float>(value->duration) : 0.0f; });
    effect.addFunc("getRemaining", [](Effect* value) { return value ? static_cast<float>(value->remaining) : 0.0f; });
    effect.addFunc("getMagnitude", [](Effect* value) { return value ? static_cast<float>(value->magnitude) : 0.0f; });
    effect.addFunc("getStackCount", [](Effect* value) { return value ? static_cast<int>(value->stackCount) : 0; });
    effect.addFunc("getPayload", [](Effect* value) -> EffectPayload* { return value ? &value->payload : nullptr; });
    effect.addFunc("addTag", [vm](Effect* value, const std::string& tag) {
        if (!value)
            return eve::script::projectResult(vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                                              "effect must not be null", "effect"));
        return eve::script::projectResult(vm, value->addTag(tag));
    });
    effect.addFunc("removeTag", [vm](Effect* value, const std::string& tag) {
        if (!value)
            return eve::script::projectResult(vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                                              "effect must not be null", "effect"));
        return eve::script::projectResult(vm, value->removeTag(tag));
    });
    effect.addFunc("hasTag", [](Effect* value, const std::string& tag) { return value && value->hasTag(tag); });
    effect.addFunc("tagCount", &Effect::tagCount);
    effect.addFunc("tagAt", &Effect::tagAt);

    auto event = table.addClass<EffectEvent>(
        "EffectEvent", std::function<EffectEvent*()>([]() -> EffectEvent* { return nullptr; }), false);
    event.addFunc("getSequence", [](EffectEvent* value) {
        return value ? static_cast<std::int64_t>(value->sequence) : std::int64_t{0};
    });
    event.addFunc("getKind", [](EffectEvent* value) { return value ? eventKindName(value->kind) : std::string{}; });
    event.addFunc("getEffectId", [](EffectEvent* value) { return value ? value->effectId : std::string{}; });
    event.addFunc("getSubject", [](EffectEvent* value) { return value ? value->subject : std::string{}; });
    event.addFunc("getType", [](EffectEvent* value) { return value ? value->type : std::string{}; });
    event.addFunc("getSource", [](EffectEvent* value) { return value ? value->source : std::string{}; });
    event.addFunc("getReason", [](EffectEvent* value) { return value ? value->reason : std::string{}; });

    auto container = table.addClass<EffectContainer>(
        "EffectContainer", std::function<EffectContainer*()>([]() -> EffectContainer* { return nullptr; }), false);
    container.addFunc("apply", [vm](EffectContainer* value, const std::string& subject, const std::string& type,
                                    const std::string& source, int priority, float duration,
                                    const std::string& stackKey, const std::string& policyNameValue) {
        if (!value)
            return eve::script::projectResult(
                vm,
                effectsBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                   "effect container must not be null", "container"),
                [](std::string&& id) { return eve::Value(std::move(id)); });
        auto definition = effectDefinitionFromBinding(type, priority, duration, stackKey, policyNameValue);
        if (!definition.ok())
            return eve::script::projectResult(
                vm,
                effectsBindingFailure<std::string>(definition.code() == eve::StatusCode::Rejected
                                                       ? eve::DiagnosticCode::InvalidArgument
                                                       : eve::DiagnosticCode::Failed,
                                                   definition.status().describe(), "definition"),
                [](std::string&& id) { return eve::Value(std::move(id)); });
        EffectDefinition input = std::move(definition).takeValue();
        return eve::script::projectResult(vm, value->apply(input, subject, source),
                                          [](std::string&& id) { return eve::Value(std::move(id)); });
    });
    container.addFunc(
        "applyCanonical",
        [vm](EffectContainer* value, const std::string& subject, const std::string& type, const std::string& source,
             int priority, float duration, const std::string& stackKey, const std::string& policyNameValue) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<eve::EffectId>(eve::DiagnosticCode::InvalidArgument,
                                                         "effect container must not be null", "container"),
                    [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
            auto definition = effectDefinitionFromBinding(type, priority, duration, stackKey, policyNameValue);
            if (!definition.ok())
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<eve::EffectId>(definition.code() == eve::StatusCode::Rejected
                                                             ? eve::DiagnosticCode::InvalidArgument
                                                             : eve::DiagnosticCode::Failed,
                                                         definition.status().describe(), "definition"),
                    [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
            EffectDefinition input = std::move(definition).takeValue();
            return eve::script::projectResult(vm, value->applyCanonical(input, subject, source), [](eve::EffectId id) {
                return eve::Value(id.isNil() ? std::string{} : id.format());
            });
        });
    container.addFunc("remove", [vm](EffectContainer* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "effect container must not be null", "container"));
        return eve::script::projectResult(vm, value->remove(id, reason));
    });
    container.addFunc("update", [vm](EffectContainer* value, float dtSeconds) {
        if (!value)
            return eve::script::projectResult(
                vm,
                effectsBindingFailure<EffectUpdateSummary>(eve::DiagnosticCode::InvalidArgument,
                                                           "effect container must not be null", "container"),
                [](EffectUpdateSummary summary) { return effectUpdateProjection(summary); });
        return eve::script::projectResult(vm, value->update(dtSeconds),
                                          [](EffectUpdateSummary summary) { return effectUpdateProjection(summary); });
    });
    container.addFunc("clear", &EffectContainer::clear);
    container.addFunc("find", [](EffectContainer* value, const std::string& id) -> Effect* {
        return value ? value->find(id) : nullptr;
    });
    container.addFunc("effectCount", &EffectContainer::effectCount);
    container.addFunc("effectAt", [](EffectContainer* value, int index) -> Effect* {
        return value ? value->effectAt(index) : nullptr;
    });
    container.addFunc("subjectCount", &EffectContainer::subjectCount);
    container.addFunc("subjectAt", [](EffectContainer* value, const std::string& subject, int index) -> Effect* {
        return value ? value->subjectAt(subject, index) : nullptr;
    });
    container.addFunc("taggedCount", &EffectContainer::taggedCount);
    container.addFunc("taggedAt",
                      [](EffectContainer* value, const std::string& subject, const std::string& tag,
                         int index) -> Effect* { return value ? value->taggedAt(subject, tag, index) : nullptr; });
    container.addFunc("eventCount", &EffectContainer::eventCount);
    container.addFunc("eventAt", [](EffectContainer* value, int index) -> EffectEvent* {
        return value ? value->eventAt(index) : nullptr;
    });
    container.addFunc("clearEvents", &EffectContainer::clearEvents);

    // Canonical script ownership path. The proxy is owned by Squirrel through
    // a release hook; each operation resolves its RuntimeHandleRef as a
    // Borrowed observation and therefore detects release/unload staleness.
    auto ownedContainer = table.addClass<ScriptEffectContainer>(
        "EffectContainerProxy",
        std::function<ScriptEffectContainer*()>([]() -> ScriptEffectContainer* { return nullptr; }), false);
    ownedContainer.addFunc("ownership", [](ScriptEffectContainer*) {
        return std::string(eve::script::objectSemanticName(eve::script::ObjectSemantic::Owned));
    });
    ownedContainer.addFunc(
        "handle", [](ScriptEffectContainer* value) { return value ? value->reference.packed() : std::uint64_t{0}; });
    ownedContainer.addFunc("isStale",
                           [](ScriptEffectContainer* value) { return !value || Effects::isStale(value->reference); });
    ownedContainer.addFunc("release", [vm](ScriptEffectContainer* value) {
        if (!value)
            return eve::script::projectResult(
                vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "owned effect container proxy must not be null", "container"));
        auto result = Effects::release(value->reference);
        // Retain the coordinates after release so the same script object can
        // report a stale handle and a second release returns StaleHandle.
        return eve::script::projectResult(vm, std::move(result));
    });
    ownedContainer.addFunc(
        "apply", [vm](ScriptEffectContainer* value, const std::string& subject, const std::string& type,
                      const std::string& source, int priority, float duration, const std::string& stackKey,
                      const std::string& policyNameValue) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                       "owned effect container proxy must not be null", "container"),
                    [](std::string&& id) { return eve::Value(std::move(id)); });
            auto containerView = Effects::resolve(value->reference);
            if (!containerView.isBound())
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<std::string>(eve::DiagnosticCode::StaleHandle,
                                                       "owned effect container handle is stale", "container"),
                    [](std::string&& id) { return eve::Value(std::move(id)); });
            auto definition = effectDefinitionFromBinding(type, priority, duration, stackKey, policyNameValue);
            if (!definition)
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                       definition.status().describe(), "definition"),
                    [](std::string&& id) { return eve::Value(std::move(id)); });
            auto input = std::move(definition).takeValue();
            return eve::script::projectResult(vm, containerView->apply(input, subject, source),
                                              [](std::string&& id) { return eve::Value(std::move(id)); });
        });
    ownedContainer.addFunc("update", [vm](ScriptEffectContainer* value, float dt) {
        if (!value)
            return eve::script::projectResult(
                vm,
                effectsBindingFailure<EffectUpdateSummary>(
                    eve::DiagnosticCode::InvalidArgument, "owned effect container proxy must not be null", "container"),
                [](EffectUpdateSummary summary) { return effectUpdateProjection(summary); });
        auto containerView = Effects::resolve(value->reference);
        if (!containerView.isBound())
            return eve::script::projectResult(
                vm,
                effectsBindingFailure<EffectUpdateSummary>(eve::DiagnosticCode::StaleHandle,
                                                           "owned effect container handle is stale", "container"),
                [](EffectUpdateSummary summary) { return effectUpdateProjection(summary); });
        return eve::script::projectResult(vm, containerView->update(dt),
                                          [](EffectUpdateSummary summary) { return effectUpdateProjection(summary); });
    });
    ownedContainer.addFunc("clear", [](ScriptEffectContainer* value) {
        if (!value) return;
        auto containerView = Effects::resolve(value->reference);
        if (containerView.isBound()) containerView->clear();
    });
    ownedContainer.addFunc("find", [](ScriptEffectContainer* value, const std::string& id) -> Effect* {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->find(id) : nullptr;
    });
    ownedContainer.addFunc("effectCount", [](ScriptEffectContainer* value) {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->effectCount() : 0;
    });
    ownedContainer.addFunc("effectAt", [](ScriptEffectContainer* value, int index) -> Effect* {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->effectAt(index) : nullptr;
    });
    ownedContainer.addFunc("subjectCount", [](ScriptEffectContainer* value, const std::string& subject) {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->subjectCount(subject) : 0;
    });
    ownedContainer.addFunc(
        "subjectAt", [](ScriptEffectContainer* value, const std::string& subject, int index) -> Effect* {
            auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
            return containerView.isBound() ? containerView->subjectAt(subject, index) : nullptr;
        });
    ownedContainer.addFunc(
        "taggedCount", [](ScriptEffectContainer* value, const std::string& subject, const std::string& tag) {
            auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
            return containerView.isBound() ? containerView->taggedCount(subject, tag) : 0;
        });
    ownedContainer.addFunc(
        "taggedAt",
        [](ScriptEffectContainer* value, const std::string& subject, const std::string& tag, int index) -> Effect* {
            auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
            return containerView.isBound() ? containerView->taggedAt(subject, tag, index) : nullptr;
        });
    ownedContainer.addFunc("eventCount", [](ScriptEffectContainer* value) {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->eventCount() : 0;
    });
    ownedContainer.addFunc("eventAt", [](ScriptEffectContainer* value, int index) -> EffectEvent* {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        return containerView.isBound() ? containerView->eventAt(index) : nullptr;
    });
    ownedContainer.addFunc("clearEvents", [](ScriptEffectContainer* value) {
        auto containerView = value ? Effects::resolve(value->reference) : eve::script::Borrowed<EffectContainer>();
        if (containerView.isBound()) containerView->clearEvents();
    });
    ownedContainer.addFunc(
        "remove", [vm](ScriptEffectContainer* value, const std::string& id, const std::string& reason) {
            if (!value)
                return eve::script::projectResult(
                    vm, effectsBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "owned effect container proxy must not be null", "container"));
            auto containerView = Effects::resolve(value->reference);
            if (!containerView.isBound())
                return eve::script::projectResult(
                    vm, effectsBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                    "owned effect container handle is stale", "container"));
            return eve::script::projectResult(vm, containerView->remove(id, reason));
        });
    ownedContainer.addFunc(
        "applyCanonical", [vm](ScriptEffectContainer* value, const std::string& subject, const std::string& type,
                               const std::string& source, int priority, float duration, const std::string& stackKey,
                               const std::string& policyNameValue) {
            if (!value)
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<eve::EffectId>(eve::DiagnosticCode::InvalidArgument,
                                                         "owned effect container proxy must not be null", "container"),
                    [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
            auto containerView = Effects::resolve(value->reference);
            if (!containerView.isBound())
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<eve::EffectId>(eve::DiagnosticCode::StaleHandle,
                                                         "owned effect container handle is stale", "container"),
                    [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
            auto definition = effectDefinitionFromBinding(type, priority, duration, stackKey, policyNameValue);
            if (!definition.ok())
                return eve::script::projectResult(
                    vm,
                    effectsBindingFailure<eve::EffectId>(eve::DiagnosticCode::InvalidArgument,
                                                         definition.status().describe(), "definition"),
                    [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
            auto input = std::move(definition).takeValue();
            return eve::script::projectResult(
                vm, containerView->applyCanonical(input, subject, source),
                [](eve::EffectId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
        });

    auto cls = table.addClass(name, Effects::create, false);
    expose(cls);
}

void Effects::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Effects::getName);
    cls.addFunc("newContainer", [vm = cls.getHandle()](Effects*) -> ssq::Table {
        return makeOwnedProxy<EffectContainerHandleRef, ScriptEffectContainer>(
            vm, Effects::newContainer(), [](EffectContainerHandleRef ref) { return Effects::release(ref); });
    });
}

}  // namespace eve::effects
