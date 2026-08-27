#include "rpg/SkillAction.h"
#include <string>
#include <utility>

namespace eve::rpg {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<eve::LogicalId> skillActionId(const SkillDefinition& skill) {
    if (skill.id.empty())
        return failure<eve::LogicalId>(eve::DiagnosticCode::InvalidArgument,
                                      "skill id must not be empty", "skill.id");
    const auto id = eve::LogicalId::fromParts("rpg", "skill." + skill.id);
    if (!id)
        return failure<eve::LogicalId>(eve::DiagnosticCode::InvalidArgument,
                                      "skill id cannot form a valid action logical id", "skill.id");
    return eve::Result<eve::LogicalId>::success(*id);
}

eve::Result<eve::Duration> seconds(float value, const char* path) {
    auto duration = eve::Duration::fromSeconds(static_cast<double>(value));
    if (!duration) {
        const eve::Status status = duration.status();
        (void)status;
        return eve::Result<eve::Duration>::failure(duration.status());
    }
    if (duration.value().nanoseconds() < 0)
        return failure<eve::Duration>(eve::DiagnosticCode::InvalidArgument,
                                      "skill action duration must be non-negative", path);
    return duration;
}

}  // namespace

eve::Result<action::ActionDefinition> SkillActionAdapter::makeDefinition(
    const SkillDefinition& skill) {
    auto id = skillActionId(skill);
    if (!id) return eve::Result<action::ActionDefinition>::failure(id.status());
    auto windup = seconds(skill.castTime, "skill.castTime");
    if (!windup) return eve::Result<action::ActionDefinition>::failure(windup.status());
    auto cooldown = seconds(skill.cooldown, "skill.cooldown");
    if (!cooldown) return eve::Result<action::ActionDefinition>::failure(cooldown.status());

    action::ActionDefinition definition;
    definition.id = std::move(id).takeValue();
    definition.timing.windup = std::move(windup).takeValue();
    definition.condition = skill.castCondition;
    definition.targetingMode = skill.targetType == "self" ? action::TargetingMode::None
                                                              : action::TargetingMode::Explicit;
    definition.cost = skill.cost;
    definition.effectIds = skill.grantedEffects;
    // A skill is an Active operation even when it currently has no effect id;
    // the injected executor may emit a settlement/event for that operation.
    definition.activeExecutionRequired = true;
    definition.metadata.emplace("cooldown", eve::Value(cooldown.value().seconds()));
    definition.metadata.emplace("targetType", eve::Value(skill.targetType));
    definition.metadata.emplace("skillId", eve::Value(skill.id));

    auto valid = definition.validate();
    if (!valid) return eve::Result<action::ActionDefinition>::failure(valid.status());
    return eve::Result<action::ActionDefinition>::success(std::move(definition));
}

eve::Result<action::ActionRequest> SkillActionAdapter::makeRequest(
    const SkillDefinition& skill, std::optional<ecs::EntityHandle> source,
    std::optional<ecs::EntityHandle> target, eve::SimulationTick requestedTick) {
    auto definition = makeDefinition(skill);
    if (!definition) return eve::Result<action::ActionRequest>::failure(definition.status());
    auto checkedDefinition = std::move(definition).takeValue();

    if (checkedDefinition.targetingMode == action::TargetingMode::None && target)
        return failure<action::ActionRequest>(
            eve::DiagnosticCode::InvalidArgument,
            "self skill request cannot carry an explicit target", "target");
    if (checkedDefinition.targetingMode == action::TargetingMode::Explicit && !target)
        return failure<action::ActionRequest>(
            eve::DiagnosticCode::PreconditionViolation,
            "non-self skill request requires an explicit target", "target");

    action::ActionRequest request;
    request.actionId = checkedDefinition.id;
    request.source = std::move(source);
    request.requestedTick = requestedTick;
    if (target) request.targetEntities.push_back(*target);
    auto valid = request.validate(checkedDefinition);
    if (!valid) return eve::Result<action::ActionRequest>::failure(valid.status());
    return eve::Result<action::ActionRequest>::success(std::move(request));
}

eve::Result<action::ActionExecutionId> SkillActionAdapter::submit(
    action::ActionRuntime& runtime, const SkillDefinition& skill,
    std::optional<ecs::EntityHandle> source, std::optional<ecs::EntityHandle> target,
    eve::SimulationTick requestedTick) {
    auto definition = makeDefinition(skill);
    if (!definition) return eve::Result<action::ActionExecutionId>::failure(definition.status());
    auto request = makeRequest(skill, std::move(source), std::move(target), requestedTick);
    if (!request) return eve::Result<action::ActionExecutionId>::failure(request.status());
    return runtime.submit(std::move(definition).takeValue(), std::move(request).takeValue());
}

}  // namespace eve::rpg
