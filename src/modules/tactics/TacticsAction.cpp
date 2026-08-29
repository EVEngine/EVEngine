#include "tactics/TacticsAction.h"

#include "common/Assert.h"

#include <cstdint>
#include <charconv>
#include <limits>
#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<int> integerParameter(const Value::Object& parameters, std::string_view key) {
    const auto found = parameters.find(std::string(key));
    if (found == parameters.end() || !found->second.isInt64())
        return failure<int>(DiagnosticCode::InvalidArgument, "tactics move parameter must be an integer",
                            "request.parameters." + std::string(key));
    const std::int64_t value = found->second.asInt();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument, "tactics move parameter is outside int range",
                            "request.parameters." + std::string(key));
    return Result<int>::success(static_cast<int>(value));
}

Result<Revision> revisionParameter(const Value::Object& parameters) {
    const auto found = parameters.find("expectedRevision");
    const auto* text = found == parameters.end() ? nullptr : found->second.getIf<std::string>();
    if (!text)
        return failure<Revision>(DiagnosticCode::InvalidArgument,
                                 "tactics move requires an expected revision decimal string",
                                 "request.parameters.expectedRevision");
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (error != std::errc{} || end != text->data() + text->size())
        return failure<Revision>(DiagnosticCode::InvalidArgument, "tactics expected revision is invalid",
                                 "request.parameters.expectedRevision");
    return Result<Revision>::success(Revision(value));
}

class PreparedMoveOperation final : public action::IActionEffectOperation {
public:
    PreparedMoveOperation(Battle& battle, SubjectRef actor, Cell destination, SimulationTick tick)
        : battle_(battle), actor_(actor), destination_(destination), tick_(tick) {}

    void commit() noexcept override {
        auto committed = BattleSystem::moveUnit(battle_, actor_, destination_, tick_);
        const bool ok  = committed.ok();
        EV_ASSERT(ok, "prepared tactics move changed between synchronous prepare and commit");
        if (ok) std::move(committed).takeValue();
    }

    void rollback() noexcept override {}

private:
    Battle&    battle_;
    SubjectRef actor_;
    Cell       destination_;
    SimulationTick tick_;
};

class PreparedFaceOperation final : public action::IActionEffectOperation {
public:
    PreparedFaceOperation(Battle& battle, SubjectRef actor, int facing, SimulationTick tick)
        : battle_(battle), actor_(actor), facing_(facing), tick_(tick) {}

    void commit() noexcept override {
        auto committed = BattleSystem::faceUnit(battle_, actor_, facing_, tick_);
        const bool ok = committed.ok();
        EV_ASSERT(ok, "prepared tactics face changed between synchronous prepare and commit");
    }

    void rollback() noexcept override {}

private:
    Battle&        battle_;
    SubjectRef     actor_;
    int            facing_ = 0;
    SimulationTick tick_;
};

class PreparedWaitOperation final : public action::IActionEffectOperation {
public:
    PreparedWaitOperation(Battle& battle, SubjectRef actor, SimulationTick tick)
        : battle_(battle), actor_(actor), tick_(tick) {}

    void commit() noexcept override {
        auto committed = BattleSystem::waitUnit(battle_, actor_, tick_);
        const bool ok = committed.ok();
        EV_ASSERT(ok, "prepared tactics wait changed between synchronous prepare and commit");
    }

    void rollback() noexcept override {}

private:
    Battle&        battle_;
    SubjectRef     actor_;
    SimulationTick tick_;
};

Result<std::pair<TacticalUnit*, Battle*>> requestOwners(ecs::EntityHandle unit) {
    auto* tacticalUnit = dynamic_cast<TacticalUnit*>(ecs::try_get(unit));
    if (!tacticalUnit)
        return failure<std::pair<TacticalUnit*, Battle*>>(
            DiagnosticCode::StaleHandle, "tactics action source is stale or has the wrong type", "unit");
    auto* battle = dynamic_cast<Battle*>(ecs::try_get(tacticalUnit->membership()->battle));
    if (!battle)
        return failure<std::pair<TacticalUnit*, Battle*>>(DiagnosticCode::StaleHandle,
                                                          "tactics action source battle is stale", "unit.battle");
    return Result<std::pair<TacticalUnit*, Battle*>>::success({tacticalUnit, battle});
}

action::ActionRequest baseRequest(LogicalId id, ecs::EntityHandle unit, Battle& battle, SimulationTick tick) {
    action::ActionRequest request;
    request.actionId = std::move(id);
    request.source = unit;
    request.requestedTick = tick;
    request.parameters.emplace("expectedRevision", Value(std::to_string(battle.turn()->revision.value())));
    return request;
}

}  // namespace

Result<std::unique_ptr<action::IActionEffectOperation>> TacticsActionExecutor::prepare(
    const action::ActionDefinition& definition, const action::ActionRequest& request, const sensing::TargetSet* targets,
    SimulationTick tick) {
    (void)targets;
    const auto moveId = LogicalId::parse("tactics:move");
    const auto faceId = LogicalId::parse("tactics:face");
    const auto waitId = LogicalId::parse("tactics:wait");
    if (!moveId || !faceId || !waitId || definition.id != request.actionId ||
        (definition.id != *moveId && definition.id != *faceId && definition.id != *waitId))
        return failure<std::unique_ptr<action::IActionEffectOperation>>(
            DiagnosticCode::InvalidArgument, "tactics executor received an unsupported action definition",
            "definition.id");
    if (!request.source)
        return failure<std::unique_ptr<action::IActionEffectOperation>>(
            DiagnosticCode::InvalidArgument, "tactics action request requires a source unit", "request.source");
    auto* unit = dynamic_cast<TacticalUnit*>(ecs::try_get(*request.source));
    if (unit == nullptr)
        return failure<std::unique_ptr<action::IActionEffectOperation>>(
            DiagnosticCode::StaleHandle, "tactics action source is stale or has the wrong type", "request.source");
    auto expectedRevision = revisionParameter(request.parameters);
    if (!expectedRevision)
        return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(expectedRevision.status());
    if (battle_.turn()->revision != expectedRevision.value())
        return failure<std::unique_ptr<action::IActionEffectOperation>>(
            DiagnosticCode::Conflict, "tactics action request was prepared against an older battle revision",
            "request.parameters.expectedRevision");

    if (definition.id == *moveId) {
        auto x = integerParameter(request.parameters, "x");
        auto y = integerParameter(request.parameters, "y");
        auto layer = integerParameter(request.parameters, "layer");
        if (!x) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(x.status());
        if (!y) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(y.status());
        if (!layer) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(layer.status());
        const Cell destination{x.value(), y.value(), layer.value()};
        auto preview = BattleSystem::previewMove(battle_, unit->identity()->subject, destination);
        if (!preview) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(preview.status());
        std::move(preview).takeValue();
        return Result<std::unique_ptr<action::IActionEffectOperation>>::success(
            std::make_unique<PreparedMoveOperation>(battle_, unit->identity()->subject, destination, tick),
            Status::success(StatusCode::Applied));
    }
    if (definition.id == *faceId) {
        auto facing = integerParameter(request.parameters, "facing");
        if (!facing) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(facing.status());
        auto preview = BattleSystem::previewFace(battle_, unit->identity()->subject, facing.value());
        if (!preview) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(preview.status());
        return Result<std::unique_ptr<action::IActionEffectOperation>>::success(
            std::make_unique<PreparedFaceOperation>(battle_, unit->identity()->subject, facing.value(), tick),
            Status::success(StatusCode::Applied));
    }
    auto preview = BattleSystem::previewWait(battle_, unit->identity()->subject);
    if (!preview) return Result<std::unique_ptr<action::IActionEffectOperation>>::failure(preview.status());
    return Result<std::unique_ptr<action::IActionEffectOperation>>::success(
        std::make_unique<PreparedWaitOperation>(battle_, unit->identity()->subject, tick),
        Status::success(StatusCode::Applied));
}

action::ActionDefinition moveActionDefinition() {
    action::ActionDefinition definition;
    definition.id                      = *LogicalId::parse("tactics:move");
    definition.activeExecutionRequired = true;
    return definition;
}

action::ActionDefinition faceActionDefinition() {
    action::ActionDefinition definition;
    definition.id = *LogicalId::parse("tactics:face");
    definition.activeExecutionRequired = true;
    return definition;
}

action::ActionDefinition waitActionDefinition() {
    action::ActionDefinition definition;
    definition.id = *LogicalId::parse("tactics:wait");
    definition.activeExecutionRequired = true;
    return definition;
}

Result<action::ActionRequest> makeMoveRequest(ecs::EntityHandle unit, Cell destination, SimulationTick tick) {
    auto owners = requestOwners(unit);
    if (!owners) return Result<action::ActionRequest>::failure(owners.status());
    action::ActionRequest request = baseRequest(*LogicalId::parse("tactics:move"), unit, *owners.value().second, tick);
    request.parameters.emplace("x", Value(destination.x));
    request.parameters.emplace("y", Value(destination.y));
    request.parameters.emplace("layer", Value(destination.layer));
    return Result<action::ActionRequest>::success(std::move(request));
}

Result<action::ActionRequest> makeFaceRequest(ecs::EntityHandle unit, int facing, SimulationTick tick) {
    auto owners = requestOwners(unit);
    if (!owners) return Result<action::ActionRequest>::failure(owners.status());
    action::ActionRequest request = baseRequest(*LogicalId::parse("tactics:face"), unit, *owners.value().second, tick);
    request.parameters.emplace("facing", Value(facing));
    return Result<action::ActionRequest>::success(std::move(request));
}

Result<action::ActionRequest> makeWaitRequest(ecs::EntityHandle unit, SimulationTick tick) {
    auto owners = requestOwners(unit);
    if (!owners) return Result<action::ActionRequest>::failure(owners.status());
    return Result<action::ActionRequest>::success(
        baseRequest(*LogicalId::parse("tactics:wait"), unit, *owners.value().second, tick));
}

}  // namespace eve::tactics
