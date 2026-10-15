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

template <class MemFn, class... Args>
ssq::Table invoke(const script::BindContext& bind, ScriptActionRuntime* self, MemFn method, Args&&... args) {
    if (!self) return bind.failInvalid("action runtime must not be null", "runtime");
    return projectValue(bind.vm(), (self->*method)(std::forward<Args>(args)...));
}

ssq::Table newRuntime(const script::BindContext& bind) {
    auto object =
        script::makeOwnedSquirrelInstance<ScriptActionRuntime>(bind.vm(), std::make_unique<ScriptActionRuntime>());
    if (!object) return script::projectStatusResult(bind.vm(), object.status());
    auto result = script::projectStatusResult(bind.vm(), Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

void exposeActionScriptBindings(ssq::Table& table) {
    const script::BindContext bind{table.getHandle(), kSource};
    auto                      runtime = table.addClass<ScriptActionRuntime>(
        "ActionRuntime", std::function<ScriptActionRuntime*()>([] { return nullptr; }), true);

    script::bindMethod(runtime, "ownership", [](ScriptActionRuntime*) { return std::string("owned"); });
    script::bindMethod(runtime, "registerAbilityJson", [bind](ScriptActionRuntime* self, std::string json) {
        return invoke(bind, self, &ScriptActionRuntime::registerAbilityJson, json);
    });
    script::bindMethod(runtime, "replaceAbilityJson", [bind](ScriptActionRuntime* self, std::string json) {
        return invoke(bind, self, &ScriptActionRuntime::replaceAbilityJson, json);
    });
    script::bindMethod(
        runtime, "registerTimelineAbility",
        [bind](ScriptActionRuntime* self, std::string definitionId, std::string timelineJson, float cooldownSeconds,
               std::string instancing, std::string activationGroup, std::string triggerTag) {
            return invoke(bind, self, &ScriptActionRuntime::registerTimelineAbility, definitionId, timelineJson,
                          cooldownSeconds, instancing, activationGroup, triggerTag);
        });
    script::bindMethod(
        runtime, "replaceTimelineAbility",
        [bind](ScriptActionRuntime* self, std::string definitionId, std::string timelineJson, float cooldownSeconds,
               std::string instancing, std::string activationGroup, std::string triggerTag) {
            return invoke(bind, self, &ScriptActionRuntime::replaceTimelineAbility, definitionId, timelineJson,
                          cooldownSeconds, instancing, activationGroup, triggerTag);
        });
    script::bindMethod(runtime, "grantAbility",
                       [bind](ScriptActionRuntime* self, std::string owner, std::string definitionId) {
                           return invoke(bind, self, &ScriptActionRuntime::grantAbility, owner, definitionId);
                       });
    script::bindMethod(runtime, "revokeAbility", [bind](ScriptActionRuntime* self, std::int64_t grantId) {
        return invoke(bind, self, &ScriptActionRuntime::revokeAbility, grantId);
    });
    script::bindMethod(runtime, "activateAbility",
                       [bind](ScriptActionRuntime* self, std::int64_t grantId, std::int64_t tick) {
                           return invoke(bind, self, &ScriptActionRuntime::activateAbility, grantId, tick);
                       });
    script::bindMethod(runtime, "advanceAbilities",
                       [bind](ScriptActionRuntime* self, std::int64_t tick, float seconds) {
                           return invoke(bind, self, &ScriptActionRuntime::advanceAbilities, tick, seconds);
                       });
    script::bindMethod(runtime, "cancelAbility",
                       [bind](ScriptActionRuntime* self, std::int64_t activationId, std::int64_t tick) {
                           return invoke(bind, self, &ScriptActionRuntime::cancelAbility, activationId, tick);
                       });
    script::bindMethod(runtime, "abilityGrant", [bind](ScriptActionRuntime* self, std::int64_t grantId) {
        return invoke(bind, self, &ScriptActionRuntime::abilityGrant, grantId);
    });
    script::bindMethod(runtime, "matchingAbilities",
                       [bind](ScriptActionRuntime* self, std::string owner, std::string eventTag) {
                           return invoke(bind, self, &ScriptActionRuntime::matchingAbilities, owner, eventTag);
                       });
    script::bindMethod(runtime, "submitAction",
                       [bind](ScriptActionRuntime* self, std::string actionId, std::int64_t windupNs,
                              std::int64_t activeNs, std::int64_t recoverNs, std::int64_t tick) {
                           return invoke(bind, self, &ScriptActionRuntime::submitAction, actionId, windupNs, activeNs,
                                         recoverNs, tick);
                       });
    script::bindMethod(runtime, "advanceAction",
                       [bind](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick, float seconds) {
                           return invoke(bind, self, &ScriptActionRuntime::advanceAction, executionId, tick, seconds);
                       });
    script::bindMethod(runtime, "cancelAction",
                       [bind](ScriptActionRuntime* self, std::int64_t executionId, std::int64_t tick) {
                           return invoke(bind, self, &ScriptActionRuntime::cancelAction, executionId, tick);
                       });
    script::bindMethod(runtime, "findAction", [bind](ScriptActionRuntime* self, std::int64_t executionId) {
        return invoke(bind, self, &ScriptActionRuntime::findAction, executionId);
    });
    script::bindNullSafe<ScriptActionRuntime>(
        runtime, "executionCount", [](ScriptActionRuntime& self) { return self.executionCount(); }, std::int64_t{0});

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
