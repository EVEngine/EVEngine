#include "action/ActionScriptBindings.h"

#include "action/ActionModule.h"
#include "action/ScriptActionRuntime.h"
#include "common/SquirrelBindContext.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace eve::action {
namespace {

constexpr const char* kSource = "action.squirrel";

ssq::Table projectValue(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

template <class Fn>
ssq::Table callRuntime(const script::BindContext& bind, ScriptActionRuntime* self, Fn&& fn) {
    if (!self) return bind.failInvalid("action runtime must not be null", "runtime");
    return projectValue(bind.vm(), std::forward<Fn>(fn)(*self));
}

ssq::Table newRuntime(const script::BindContext& bind) {
    auto runtime = std::make_unique<ScriptActionRuntime>();
    auto object  = script::makeOwnedSquirrelInstance<ScriptActionRuntime>(bind.vm(), std::move(runtime));
    if (!object) return script::projectStatusResult(bind.vm(), object.status());
    auto result = script::projectStatusResult(bind.vm(), Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

void bindActionRuntimeClass(ssq::Class& runtime, const script::BindContext& bind) {
    script::bindMethod(runtime, "ownership", [](ScriptActionRuntime*) { return std::string("owned"); });
    script::bindMethod(runtime, "registerAbilityJson",
                       [bind](ScriptActionRuntime* self, const std::string& json) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.registerAbilityJson(json);
                           });
                       });
    script::bindMethod(runtime, "replaceAbilityJson",
                       [bind](ScriptActionRuntime* self, const std::string& json) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.replaceAbilityJson(json);
                           });
                       });
    script::bindMethod(
        runtime, "registerTimelineAbility",
        [bind](ScriptActionRuntime* self, const std::string& definitionId, const std::string& timelineJson,
               float cooldownSeconds, const std::string& instancing, const std::string& activationGroup,
               const std::string& triggerTag) {
            return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                return rt.registerTimelineAbility(definitionId, timelineJson, cooldownSeconds, instancing,
                                                  activationGroup, triggerTag);
            });
        });
    script::bindMethod(
        runtime, "replaceTimelineAbility",
        [bind](ScriptActionRuntime* self, const std::string& definitionId, const std::string& timelineJson,
               float cooldownSeconds, const std::string& instancing, const std::string& activationGroup,
               const std::string& triggerTag) {
            return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                return rt.replaceTimelineAbility(definitionId, timelineJson, cooldownSeconds, instancing,
                                                 activationGroup, triggerTag);
            });
        });
    script::bindMethod(runtime, "grantAbility",
                       [bind](ScriptActionRuntime* self, const std::string& owner, const std::string& definitionId) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.grantAbility(owner, definitionId);
                           });
                       });
    script::bindMethod(runtime, "revokeAbility", [bind](ScriptActionRuntime* self, std::int64_t grantId) {
        return callRuntime(bind, self, [&](ScriptActionRuntime& rt) { return rt.revokeAbility(grantId); });
    });
    script::bindMethod(runtime, "activateAbility",
                       [bind](ScriptActionRuntime* self, std::int64_t grantId, std::int64_t tick) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.activateAbility(grantId, tick);
                           });
                       });
    script::bindMethod(runtime, "advanceAbilities",
                       [bind](ScriptActionRuntime* self, std::int64_t tick, float seconds) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.advanceAbilities(tick, seconds);
                           });
                       });
    script::bindMethod(runtime, "cancelAbility",
                       [bind](ScriptActionRuntime* self, std::int64_t activationId, std::int64_t tick) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.cancelAbility(activationId, tick);
                           });
                       });
    script::bindMethod(runtime, "abilityGrant", [bind](ScriptActionRuntime* self, std::int64_t grantId) {
        return callRuntime(bind, self, [&](ScriptActionRuntime& rt) { return rt.abilityGrant(grantId); });
    });
    script::bindMethod(runtime, "matchingAbilities",
                       [bind](ScriptActionRuntime* self, const std::string& owner, const std::string& eventTag) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.matchingAbilities(owner, eventTag);
                           });
                       });
    script::bindMethod(runtime, "submitAction",
                       [bind](ScriptActionRuntime* self, const std::string& actionId, std::int64_t windupNs,
                              std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tick) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.submitAction(actionId, windupNs, activeNs, recoverNs, tick);
                           });
                       });
    script::bindMethod(runtime, "advanceAction",
                       [bind](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick, float seconds) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.advanceAction(executionId, tick, seconds);
                           });
                       });
    script::bindMethod(runtime, "cancelAction",
                       [bind](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick) {
                           return callRuntime(bind, self, [&](ScriptActionRuntime& rt) {
                               return rt.cancelAction(executionId, tick);
                           });
                       });
    script::bindMethod(runtime, "findAction", [bind](ScriptActionRuntime* self, std::int64_t executionId) {
        return callRuntime(bind, self, [&](ScriptActionRuntime& rt) { return rt.findAction(executionId); });
    });
    script::bindNullSafe<ScriptActionRuntime>(
        runtime, "executionCount", [](ScriptActionRuntime& self) { return self.executionCount(); },
        std::int64_t{0});
}

}  // namespace

void exposeActionScriptBindings(ssq::Table& table) {
    const script::BindContext bind{table.getHandle(), kSource};
    auto                      runtime = table.addClass<ScriptActionRuntime>(
        "ActionRuntime", std::function<ScriptActionRuntime*()>([] { return nullptr; }), true);
    bindActionRuntimeClass(runtime, bind);

    auto module = table.addClass(Action::name, Action::create, false);
    script::bindMethod(module, "getName", &Action::getName);
    script::bindMethod(module, "newRuntime", [bind](Action*) { return newRuntime(bind); });
}

void exposeActionScriptBindings(ssq::Class& cls) {
    const script::BindContext bind{cls.getHandle(), kSource};
    script::bindMethod(cls, "getName", &Action::getName);
    script::bindMethod(cls, "newRuntime", [bind](Action*) { return newRuntime(bind); });
}

}  // namespace eve::action
