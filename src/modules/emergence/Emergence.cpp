#include "emergence/Emergence.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

namespace eve::emergence {
namespace {

/** @brief Script-owned handle proxy; the rule engine remains module-owned. */
struct ScriptRuleEngine {
    explicit ScriptRuleEngine(RuleEngineHandleRef value) : reference(value) {}
    ~ScriptRuleEngine() noexcept {
        Emergence::release(reference).ignore("script emergence engine proxy destruction");
    }
    RuleEngineHandleRef reference;
};

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) {
        return eve::script::projectStatusResult(vm, reference.status());
    }
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned emergence proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned emergence allocation");
        return eve::script::projectStatusResult(vm, status);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto        result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

eve::script::Borrowed<RuleEngine> resolveOrEmpty(ScriptRuleEngine* value) {
    return value ? Emergence::resolve(value->reference) : eve::script::Borrowed<RuleEngine>();
}

}  // namespace

eve::Result<RuleEngineHandleRef> Emergence::newEngine() {
    Emergence* module = Emergence::create();
    return module->engines_.emplace(std::make_unique<RuleEngine>());
}

eve::script::Borrowed<RuleEngine> Emergence::resolve(RuleEngineHandleRef reference) noexcept {
    Emergence* module = ModuleManager::getInstance<Emergence>("Emergence");
    if (!module) return {};
    return module->engines_.resolve(reference);
}

eve::Result<void> Emergence::release(RuleEngineHandleRef reference) {
    Emergence* module = ModuleManager::getInstance<Emergence>("Emergence");
    if (!module)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Emergence module is no longer loaded", "engine", {}, "emergence"));
    return module->engines_.erase(reference);
}

bool Emergence::isStale(RuleEngineHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Emergence* module = ModuleManager::getInstance<Emergence>("Emergence");
    return !module || module->engines_.isStale(reference);
}

Module_IMPL(Emergence, new Emergence());

void Emergence::expose(ssq::Table& t) {
    const HSQUIRRELVM vm = t.getHandle();
    auto              asBool  = [](bool value) { return eve::Value(value); };
    auto              asInt   = [](int value) { return eve::Value(static_cast<std::int64_t>(value)); };
    auto              staleBool = [&]() {
        return eve::script::projectResult(
            vm,
            eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                              "owned rule engine handle is stale", "engine", {},
                                                              "emergence")),
            asBool);
    };
    auto staleInt = [&]() {
        return eve::script::projectResult(
            vm,
            eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                             "owned rule engine handle is stale", "engine", {},
                                                             "emergence")),
            asInt);
    };

    auto owned = t.addClass<ScriptRuleEngine>(
        "RuleEngine", std::function<ScriptRuleEngine*()>([]() { return nullptr; }), false);
    owned.addFunc("ownership", [](ScriptRuleEngine*) { return std::string("owned"); });
    owned.addFunc("isStale", [](ScriptRuleEngine* value) {
        return !value || Emergence::isStale(value->reference);
    });
    owned.addFunc("release", [vm](ScriptRuleEngine* value) {
        if (!value)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "rule engine must not be null", "engine", {},
                                                                      "emergence")));
        return eve::script::projectResult(vm, Emergence::release(value->reference));
    });
    owned.addFunc("replaceCatalogueJson", [vm, staleInt, asInt](ScriptRuleEngine* value, const std::string& json) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return staleInt();
        return eve::script::projectResult(vm, view->replaceCatalogueJson(json), asInt);
    });
    owned.addFunc("ruleCount", [](ScriptRuleEngine* value) {
        auto view = resolveOrEmpty(value);
        return view.isBound() ? view->ruleCount() : 0;
    });
    owned.addFunc("setValue", [vm, staleBool, asBool](ScriptRuleEngine* value, const std::string& key,
                                                      const std::string& valueJson) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return staleBool();
        auto parsed = eve::Value::fromJson(valueJson);
        if (!parsed)
            return eve::script::projectResult(vm, eve::Result<bool>::failure(parsed.status()), asBool);
        return eve::script::projectResult(vm, view->setValue(key, std::move(parsed).takeValue()), asBool);
    });
    owned.addFunc("setTag", [vm, staleBool, asBool](ScriptRuleEngine* value, const std::string& tag, bool present) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return staleBool();
        return eve::script::projectResult(vm, view->setTag(tag, present), asBool);
    });
    owned.addFunc("setResource",
                  [vm, staleBool, asBool](ScriptRuleEngine* value, const std::string& key, std::int64_t amount) {
                      auto view = resolveOrEmpty(value);
                      if (!view.isBound()) return staleBool();
                      return eve::script::projectResult(vm, view->setResource(key, eve::Value(amount)), asBool);
                  });
    owned.addFunc("setState", [vm, staleBool, asBool](ScriptRuleEngine* value, const std::string& key,
                                                      const std::string& valueJson) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return staleBool();
        auto parsed = eve::Value::fromJson(valueJson);
        if (!parsed)
            return eve::script::projectResult(vm, eve::Result<bool>::failure(parsed.status()), asBool);
        return eve::script::projectResult(vm, view->setState(key, std::move(parsed).takeValue()), asBool);
    });
    owned.addFunc("drain", [vm, staleInt, asInt](ScriptRuleEngine* value, std::int64_t tick) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return staleInt();
        return eve::script::projectResult(
            vm, view->drain(static_cast<std::uint64_t>(std::max<std::int64_t>(0, tick))), asInt);
    });
    owned.addFunc("activationCount", [](ScriptRuleEngine* value) {
        auto view = resolveOrEmpty(value);
        return view.isBound() ? view->activationCount() : 0;
    });
    owned.addFunc("activationRuleId", [](ScriptRuleEngine* value, int index) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound()) return std::string{};
        const auto* activation = view->activationAt(index);
        return activation ? activation->ruleId : std::string{};
    });
    owned.addFunc("clearActivations", [](ScriptRuleEngine* value) {
        auto view = resolveOrEmpty(value);
        if (view.isBound()) view->clearActivations();
    });
    owned.addFunc("lastDrainEvaluations", [](ScriptRuleEngine* value) {
        auto view = resolveOrEmpty(value);
        return view.isBound() ? static_cast<std::int64_t>(view->lastDrainEvaluations()) : std::int64_t{0};
    });
    owned.addFunc("snapshotJson", [](ScriptRuleEngine* value) {
        auto view = resolveOrEmpty(value);
        return view.isBound() ? view->snapshotJson() : std::string{};
    });
    owned.addFunc("restoreJson", [vm](ScriptRuleEngine* value, const std::string& json) {
        auto view = resolveOrEmpty(value);
        if (!view.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                      "owned rule engine handle is stale", "engine", {},
                                                                      "emergence")));
        return eve::script::projectResult(vm, view->restoreJson(json));
    });

    auto c = t.addClass(name, Emergence::create, false);
    expose(c);
}

void Emergence::expose(ssq::Class& c) {
    c.addFunc("getName", &Emergence::getName);
    c.addFunc("newEngine", [vm = c.getHandle()](Emergence*) -> ssq::Table {
        return makeOwnedProxy<RuleEngineHandleRef, ScriptRuleEngine>(
            vm, Emergence::newEngine(), [](RuleEngineHandleRef ref) { return Emergence::release(ref); });
    });
}

}  // namespace eve::emergence
