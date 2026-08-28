#include "tactics/TacticsBattle.h"

#include <algorithm>
#include <utility>

namespace eve::tactics {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

template <typename T>
T* resolve(const ecs::EntityHandle& handle) noexcept {
    return dynamic_cast<T*>(ecs::try_get(handle));
}

bool containsHandle(const std::vector<ecs::EntityHandle>& values, const ecs::EntityHandle& handle) {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) { return sameHandle(value, handle); });
}

void emit(Battle& battle, BattlePhase from, BattlePhase to, SimulationTick tick, std::string type,
          SubjectRef subject = {}) {
    auto events = battle.events();
    events->values.push_back(
        BattleEvent{events->nextSequence++, 0, 0, from, to, tick, std::move(type), subject});
}

Result<Revision> nextRevision(Battle& battle) {
    const auto next = battle.turn()->revision.incremented();
    if (!next)
        return failure<Revision>(DiagnosticCode::PreconditionViolation,
                                 "tactics battle revision is exhausted", "battle.revision");
    return Result<Revision>::success(*next);
}

void record(Battle& battle, BattleCommand command) {
    auto commands = battle.commands();
    command.sequence = commands->nextSequence++;
    command.resultingRevision = battle.turn()->revision;
    std::uint64_t correlation = command.sequence;
    if (command.triggerSequence != 0) {
        const auto trigger = std::find_if(battle.events()->values.begin(), battle.events()->values.end(),
                                          [&](const auto& event) { return event.sequence == command.triggerSequence; });
        if (trigger != battle.events()->values.end())
            correlation = trigger->correlationCommand != 0 ? trigger->correlationCommand
                                                            : trigger->causationCommand;
        if (correlation == 0) correlation = command.sequence;
    }
    for (auto& event : battle.events()->values) {
        if (event.causationCommand != 0) continue;
        event.causationCommand = command.sequence;
        event.correlationCommand = correlation;
    }
    commands->values.push_back(std::move(command));
}

SubjectRef sideSubject(const ecs::EntityHandle& handle) {
    auto* side = resolve<TacticalSide>(handle);
    return side ? side->identity()->subject : SubjectRef::nil();
}

bool objectiveSatisfied(Battle& battle, const ObjectiveSpec& objective) {
    switch (objective.kind) {
        case ObjectiveKind::EliminateSide:
            for (const auto& handle : battle.turn()->units) {
                auto* unit = resolve<TacticalUnit>(handle);
                if (unit && unit->turn()->alive && sideSubject(unit->membership()->side) == objective.targetSide)
                    return false;
            }
            return true;
        case ObjectiveKind::SurviveRounds: return battle.turn()->round >= objective.requiredRound;
        case ObjectiveKind::OccupyCells:
            for (const Cell cell : objective.requiredCells) {
                const auto occupant = battle.board()->value.occupant(cell);
                if (!occupant) return false;
                TacticalUnit* occupyingUnit = nullptr;
                for (const auto& handle : battle.turn()->units) {
                    auto* unit = resolve<TacticalUnit>(handle);
                    if (unit && unit->identity()->subject == *occupant) {
                        occupyingUnit = unit;
                        break;
                    }
                }
                if (!occupyingUnit || !occupyingUnit->turn()->alive ||
                    sideSubject(occupyingUnit->membership()->side) != objective.beneficiarySide)
                    return false;
            }
            return true;
    }
    return false;
}

bool evaluateObjectives(Battle& battle, Revision revision) {
    bool endsBattle = false;
    for (auto& objective : battle.objectives()->values) {
        if (objective.status != ObjectiveStatus::Pending || !objectiveSatisfied(battle, objective.spec)) continue;
        objective.status = ObjectiveStatus::Completed;
        objective.completedRevision = revision;
        emit(battle, battle.turn()->phase, battle.turn()->phase, battle.turn()->tick, "objective.completed",
             objective.spec.beneficiarySide);
        endsBattle = endsBattle || objective.spec.endsBattle;
    }
    return endsBattle;
}

std::uint64_t initialStreamState(std::uint64_t seed, std::string_view stream) noexcept {
    std::uint64_t hash = 14695981039346656037ull ^ seed;
    for (const unsigned char byte : stream) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t splitMix64(std::uint64_t& state) noexcept {
    std::uint64_t value = (state += 0x9e3779b97f4a7c15ull);
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

Result<BattlePhase> transition(Battle& battle, BattlePhase next, SimulationTick tick, std::string type,
                               Revision revision, BattleCommand command, SubjectRef subject = {}) {
    auto turn = battle.turn();
    const auto from = turn->phase;
    turn->phase     = next;
    turn->tick      = tick;
    turn->revision  = revision;
    emit(battle, from, next, tick, std::move(type), subject);
    record(battle, std::move(command));
    return Result<BattlePhase>::success(next, Status::success(StatusCode::Applied));
}

}  // namespace

Result<void> BattleSystem::addSide(Battle& battle, ecs::EntityHandle sideHandle) {
    if (battle.turn()->status != BattleStatus::Setup)
        return failure(DiagnosticCode::PreconditionViolation, "tactics sides can only be added during setup",
                       "battle.status");
    auto* side = resolve<TacticalSide>(sideHandle);
    if (side == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics side handle is stale or has the wrong type", "side");
    auto& sides = battle.turn()->sides;
    if (containsHandle(sides, sideHandle))
        return failure(DiagnosticCode::Conflict, "tactics side is already registered", "side");
    for (const auto& existingHandle : sides) {
        auto* existing = resolve<TacticalSide>(existingHandle);
        if (!existing)
            return failure(DiagnosticCode::StaleHandle, "tactics battle contains a stale side", "battle.sides");
        if (existing->identity()->subject == side->identity()->subject)
            return failure(DiagnosticCode::Conflict, "tactics side subject is already registered", "side.subject");
    }
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    sides.push_back(sideHandle);
    battle.turn()->revision = std::move(revision).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::addUnit(Battle& battle, ecs::EntityHandle unitHandle, ecs::EntityHandle sideHandle,
                                   Cell cellValue) {
    if (battle.turn()->status != BattleStatus::Setup)
        return failure(DiagnosticCode::PreconditionViolation, "tactics units can only be added during setup",
                       "battle.status");
    TacticalUnit* unit = resolve<TacticalUnit>(unitHandle);
    if (unit == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics unit handle is stale or has the wrong type", "unit");
    if (resolve<TacticalSide>(sideHandle) == nullptr || !containsHandle(battle.turn()->sides, sideHandle))
        return failure(DiagnosticCode::NotFound, "tactics unit side is not registered with the battle", "side");
    if (containsHandle(battle.turn()->units, unitHandle))
        return failure(DiagnosticCode::Conflict, "tactics unit is already registered", "unit");
    if (!unit->identity()->subject.isValid())
        return failure(DiagnosticCode::InvalidArgument, "tactics unit requires a valid SubjectRef", "unit.subject");
    for (const auto& existingHandle : battle.turn()->units) {
        auto* existing = resolve<TacticalUnit>(existingHandle);
        if (!existing)
            return failure(DiagnosticCode::StaleHandle, "tactics battle contains a stale unit", "battle.units");
        if (existing->identity()->subject == unit->identity()->subject)
            return failure(DiagnosticCode::Conflict, "tactics unit subject is already registered", "unit.subject");
    }

    auto placed = battle.board()->value.place(unit->identity()->subject, cellValue);
    if (!placed) return Result<void>::failure(placed.status());

    auto revision = nextRevision(battle);
    if (!revision) {
        battle.board()->value.remove(unit->identity()->subject).ignore("rollback tactics unit placement");
        return Result<void>::failure(revision.status());
    }

    unit->membership()->battle = battle.identity()->self;
    unit->membership()->side   = sideHandle;
    unit->position()->cell     = cellValue;
    unit->position()->placed   = true;
    battle.turn()->units.push_back(unitHandle);
    battle.turn()->revision = std::move(revision).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::start(Battle& battle, TurnPolicyKind policy) {
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    if (turn->status != BattleStatus::Setup)
        return failure(DiagnosticCode::Conflict, "tactics battle has already started", "battle.status");
    if (turn->sides.empty() || turn->units.empty())
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle requires sides and units",
                       "battle.setup");
    auto boardValid = battle.board()->value.validateInvariants();
    if (!boardValid) return Result<void>::failure(boardValid.status());
    auto units = orderedUnits(battle);
    if (!units) return Result<void>::failure(units.status());
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());

    turn->policy = policy;
    turn->status = BattleStatus::Running;
    turn->phase  = BattlePhase::BattleStart;
    turn->tick   = SimulationTick::zero();
    turn->round  = 0;
    turn->cursor = 0;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Setup, BattlePhase::BattleStart, turn->tick, "battle.started",
         battle.identity()->subject);
    BattleCommand command;
    command.kind = BattleCommandKind::Start;
    command.expectedRevision = expectedRevision;
    command.policy = policy;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<BattlePhase> BattleSystem::advance(Battle& battle, const SimulationStep& step) {
    auto turn = battle.turn();
    if (turn->status != BattleStatus::Running)
        return failure<BattlePhase>(DiagnosticCode::PreconditionViolation, "tactics battle is not running",
                                    "battle.status");
    if (step.delta.nanoseconds() < 0 || step.tick <= turn->tick)
        return failure<BattlePhase>(DiagnosticCode::InvalidArgument,
                                    "tactics step requires a non-negative delta and increasing tick", "step");
    auto revision = nextRevision(battle);
    if (!revision) return Result<BattlePhase>::failure(revision.status());
    const Revision committedRevision = std::move(revision).takeValue();
    BattleCommand command;
    command.kind = BattleCommandKind::Advance;
    command.expectedRevision = turn->revision;
    command.step = step;

    switch (turn->phase) {
        case BattlePhase::BattleStart:
            return transition(battle, BattlePhase::RoundStart, step.tick, "round.pending", committedRevision,
                              std::move(command));
        case BattlePhase::RoundStart: {
            auto ordered = orderedUnits(battle);
            if (!ordered) return Result<BattlePhase>::failure(ordered.status());
            turn->units  = std::move(ordered).takeValue();
            turn->cursor = 0;
            ++turn->round;
            for (const auto& handle : turn->units) {
                TacticalUnit* unit = resolve<TacticalUnit>(handle);
                if (unit != nullptr) {
                    unit->turn()->actionPoints   = unit->turn()->roundActionPoints;
                    unit->turn()->movePoints     = unit->turn()->roundMovePoints;
                    unit->turn()->reactionPoints = unit->turn()->roundReactionPoints;
                    unit->turn()->acted          = false;
                }
            }
            if (turn->units.empty())
                return transition(battle, BattlePhase::BattleEnd, step.tick, "battle.empty", committedRevision,
                                  std::move(command));
            TacticalUnit* unit = resolve<TacticalUnit>(turn->units.front());
            turn->activeUnit   = turn->units.front();
            turn->activeSide   = unit->membership()->side;
            return transition(battle, BattlePhase::TurnStart, step.tick, "turn.pending", committedRevision,
                              std::move(command),
                              unit->identity()->subject);
        }
        case BattlePhase::TurnStart: {
            TacticalUnit* unit = turn->activeUnit ? resolve<TacticalUnit>(*turn->activeUnit) : nullptr;
            if (unit == nullptr)
                return failure<BattlePhase>(DiagnosticCode::StaleHandle, "tactics active unit is stale",
                                            "battle.activeUnit");
            return transition(battle, BattlePhase::Acting, step.tick, "turn.started", committedRevision,
                              std::move(command),
                              unit->identity()->subject);
        }
        case BattlePhase::TurnEnd: {
            auto ordered = orderedUnits(battle);
            if (!ordered) return Result<BattlePhase>::failure(ordered.status());
            turn->units = std::move(ordered).takeValue();
            ++turn->cursor;
            if (turn->cursor >= turn->units.size()) {
                turn->activeUnit.reset();
                turn->activeSide.reset();
                return transition(battle, BattlePhase::RoundEnd, step.tick, "round.pending", committedRevision,
                                  std::move(command));
            }
            TacticalUnit* unit = resolve<TacticalUnit>(turn->units[turn->cursor]);
            turn->activeUnit   = turn->units[turn->cursor];
            turn->activeSide   = unit->membership()->side;
            return transition(battle, BattlePhase::TurnStart, step.tick, "turn.pending", committedRevision,
                              std::move(command),
                              unit->identity()->subject);
        }
        case BattlePhase::RoundEnd:
            turn->tick = step.tick;
            if (evaluateObjectives(battle, committedRevision)) {
                turn->status = BattleStatus::Ended;
                return transition(battle, BattlePhase::BattleEnd, step.tick, "battle.objective_completed",
                                  committedRevision, std::move(command));
            }
            return transition(battle, BattlePhase::RoundStart, step.tick, "round.completed", committedRevision,
                              std::move(command));
        case BattlePhase::BattleEnd:
            turn->status = BattleStatus::Ended;
            turn->tick   = step.tick;
            turn->revision = committedRevision;
            record(battle, std::move(command));
            return Result<BattlePhase>::success(BattlePhase::BattleEnd, Status::success(StatusCode::NoOp));
        case BattlePhase::Acting:
        case BattlePhase::Reaction:
            return failure<BattlePhase>(DiagnosticCode::PreconditionViolation,
                                        "tactics phase requires an explicit command before advancing",
                                        "battle.phase");
        case BattlePhase::Setup:
            return failure<BattlePhase>(DiagnosticCode::InvariantViolation,
                                        "running tactics battle cannot remain in setup phase", "battle.phase");
    }
    return failure<BattlePhase>(DiagnosticCode::InvariantViolation, "tactics battle phase is invalid",
                                "battle.phase");
}

Result<void> BattleSystem::endTurn(Battle& battle, SubjectRef actor) {
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Acting || !turn->activeUnit)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle is not accepting end-turn",
                       "battle.phase");
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    if (unit == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics active unit is stale", "battle.activeUnit");
    if (unit->identity()->subject != actor)
        return failure(DiagnosticCode::PreconditionViolation, "tactics actor does not own the active turn",
                       "actor");
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    unit->turn()->acted = true;
    const auto from     = turn->phase;
    turn->phase         = BattlePhase::TurnEnd;
    turn->revision      = std::move(revision).takeValue();
    emit(battle, from, BattlePhase::TurnEnd, turn->tick, "turn.completed", actor);
    BattleCommand command;
    command.kind = BattleCommandKind::EndTurn;
    command.expectedRevision = expectedRevision;
    command.actor = actor;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<MoveReceipt> BattleSystem::moveUnit(Battle& battle, SubjectRef actor, Cell destination) {
    return moveUnit(battle, actor, destination, battle.turn()->tick);
}

Result<MoveReceipt> BattleSystem::moveUnit(Battle& battle, SubjectRef actor, Cell destination,
                                           SimulationTick commandTick) {
    if (commandTick < battle.turn()->tick)
        return failure<MoveReceipt>(DiagnosticCode::InvalidArgument,
                                    "tactics movement command tick cannot move backward", "commandTick");
    const Revision expectedRevision = battle.turn()->revision;
    auto preview = previewMove(battle, actor, destination);
    if (!preview) return Result<MoveReceipt>::failure(preview.status());
    MoveReceipt receipt = std::move(preview).takeValue();
    auto turn = battle.turn();
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    auto revision = nextRevision(battle);
    if (!revision) return Result<MoveReceipt>::failure(revision.status());

    auto moved = battle.board()->value.move(actor, destination);
    if (!moved) return Result<MoveReceipt>::failure(moved.status());
    unit->position()->cell = destination;
    unit->turn()->movePoints -= receipt.cost;
    receipt.remainingMovePoints = unit->turn()->movePoints;
    turn->tick = commandTick;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Acting, BattlePhase::Acting, turn->tick, "unit.moved", actor);
    if (evaluateObjectives(battle, turn->revision)) {
        turn->status = BattleStatus::Ended;
        turn->phase = BattlePhase::BattleEnd;
        emit(battle, BattlePhase::Acting, BattlePhase::BattleEnd, turn->tick, "battle.objective_completed",
             battle.identity()->subject);
    }
    BattleCommand command;
    command.kind = BattleCommandKind::Move;
    command.expectedRevision = expectedRevision;
    command.actor = actor;
    command.cell = destination;
    command.step.tick = commandTick;
    record(battle, std::move(command));
    return Result<MoveReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::faceUnit(Battle& battle, SubjectRef actor, int facing) {
    return faceUnit(battle, actor, facing, battle.turn()->tick);
}

Result<void> BattleSystem::faceUnit(Battle& battle, SubjectRef actor, int facing, SimulationTick commandTick) {
    if (commandTick < battle.turn()->tick)
        return failure(DiagnosticCode::InvalidArgument, "tactics facing command tick cannot move backward",
                       "commandTick");
    auto preview = previewFace(battle, actor, facing);
    if (!preview) return preview;
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    if (unit->position()->facing == facing) return Result<void>::success(Status::success(StatusCode::NoOp));
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    unit->position()->facing = facing;
    turn->tick = commandTick;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Acting, BattlePhase::Acting, turn->tick, "unit.facing_changed", actor);
    BattleCommand command;
    command.kind = BattleCommandKind::Face;
    command.expectedRevision = expectedRevision;
    command.actor = actor;
    command.facing = facing;
    command.step.tick = commandTick;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::previewFace(Battle& battle, SubjectRef actor, int facing) {
    auto turn = battle.turn();
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Acting || !turn->activeUnit)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle is not accepting facing changes",
                       "battle.phase");
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    if (!unit)
        return failure(DiagnosticCode::StaleHandle, "tactics active unit is stale", "battle.activeUnit");
    if (unit->identity()->subject != actor)
        return failure(DiagnosticCode::PreconditionViolation, "tactics actor does not own the active turn",
                       "actor");
    const int facingCount = battle.board()->value.topology() == BoardTopology::Square4   ? 4
                            : battle.board()->value.topology() == BoardTopology::Square8 ? 8
                                                                                         : 6;
    if (facing < 0 || facing >= facingCount)
        return failure(DiagnosticCode::InvalidArgument, "tactics facing is invalid for board topology", "facing");
    return Result<void>::success(
        Status::success(unit->position()->facing == facing ? StatusCode::NoOp : StatusCode::Ok));
}

Result<void> BattleSystem::waitUnit(Battle& battle, SubjectRef actor) {
    return waitUnit(battle, actor, battle.turn()->tick);
}

Result<void> BattleSystem::waitUnit(Battle& battle, SubjectRef actor, SimulationTick commandTick) {
    if (commandTick < battle.turn()->tick)
        return failure(DiagnosticCode::InvalidArgument, "tactics wait command tick cannot move backward",
                       "commandTick");
    auto preview = previewWait(battle, actor);
    if (!preview) return preview;
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    unit->turn()->acted = true;
    turn->tick = commandTick;
    turn->phase = BattlePhase::TurnEnd;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Acting, BattlePhase::TurnEnd, turn->tick, "unit.waited", actor);
    BattleCommand command;
    command.kind = BattleCommandKind::Wait;
    command.expectedRevision = expectedRevision;
    command.actor = actor;
    command.step.tick = commandTick;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::previewWait(Battle& battle, SubjectRef actor) {
    auto turn = battle.turn();
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Acting || !turn->activeUnit)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle is not accepting wait", "battle.phase");
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    if (!unit)
        return failure(DiagnosticCode::StaleHandle, "tactics active unit is stale", "battle.activeUnit");
    if (unit->identity()->subject != actor)
        return failure(DiagnosticCode::PreconditionViolation, "tactics actor does not own the active turn",
                       "actor");
    return Result<void>::success();
}

Result<MoveReceipt> BattleSystem::previewMove(Battle& battle, SubjectRef actor, Cell destination) {
    auto turn = battle.turn();
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Acting || !turn->activeUnit)
        return failure<MoveReceipt>(DiagnosticCode::PreconditionViolation,
                                    "tactics battle is not accepting movement", "battle.phase");
    TacticalUnit* unit = resolve<TacticalUnit>(*turn->activeUnit);
    if (unit == nullptr)
        return failure<MoveReceipt>(DiagnosticCode::StaleHandle, "tactics active unit is stale",
                                    "battle.activeUnit");
    if (unit->identity()->subject != actor)
        return failure<MoveReceipt>(DiagnosticCode::PreconditionViolation,
                                    "tactics actor does not own the active turn", "actor");

    auto path = PathQuery::path(battle.board()->value, actor, destination, unit->turn()->movePoints);
    if (!path) return Result<MoveReceipt>::failure(path.status());
    std::vector<Cell> cells = std::move(path).takeValue();
    auto reachable = PathQuery::reachable(battle.board()->value, actor, unit->turn()->movePoints);
    if (!reachable) return Result<MoveReceipt>::failure(reachable.status());
    auto cost = reachable.value().cost(destination);
    if (!cost) return Result<MoveReceipt>::failure(cost.status());
    const int consumed = std::move(cost).takeValue();

    return Result<MoveReceipt>::success(MoveReceipt{actor, std::move(cells), consumed,
                                                    unit->turn()->movePoints - consumed},
                                        Status::success(StatusCode::NoOp));
}

Result<void> BattleSystem::finish(Battle& battle) {
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    if (turn->status != BattleStatus::Running)
        return failure(DiagnosticCode::PreconditionViolation, "only a running tactics battle can finish",
                       "battle.status");
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    const auto from = turn->phase;
    turn->status    = BattleStatus::Ended;
    turn->phase     = BattlePhase::BattleEnd;
    turn->revision  = std::move(revision).takeValue();
    emit(battle, from, BattlePhase::BattleEnd, turn->tick, "battle.ended", battle.identity()->subject);
    BattleCommand command;
    command.kind = BattleCommandKind::Finish;
    command.expectedRevision = expectedRevision;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> BattleSystem::openReaction(Battle& battle, std::uint64_t triggerSequence,
                                                std::vector<ReactionCandidate> candidates) {
    auto turn      = battle.turn();
    const Revision expectedRevision = turn->revision;
    auto reactions = battle.reactions();
    if (turn->status != BattleStatus::Running ||
        (turn->phase != BattlePhase::Acting && turn->phase != BattlePhase::Reaction))
        return failure<std::size_t>(DiagnosticCode::PreconditionViolation,
                                    "tactics reactions require an acting or reaction phase", "battle.phase");
    if (triggerSequence == 0 || candidates.empty())
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "tactics reaction window requires a trigger and candidates", "reaction");
    const auto triggerEvent = std::find_if(battle.events()->values.begin(), battle.events()->values.end(),
                                           [&](const auto& event) { return event.sequence == triggerSequence; });
    if (triggerEvent == battle.events()->values.end())
        return failure<std::size_t>(DiagnosticCode::NotFound,
                                    "tactics reaction trigger event does not exist", "reaction.triggerSequence");
    const std::size_t depth = reactions->stack.size() + 1;
    if (depth > reactions->maxDepth) {
        return failure<std::size_t>(DiagnosticCode::PreconditionViolation,
                                    "tactics reaction stack depth exceeded", "reaction.depth");
    }
    for (const auto& candidate : candidates) {
        if (!candidate.reactor.isValid() || !candidate.action.isValid())
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "tactics reaction candidate requires valid identities", "reaction.candidate");
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        if (left.initiative != right.initiative) return left.initiative > right.initiative;
        if (left.reactor.format() != right.reactor.format()) return left.reactor.format() < right.reactor.format();
        return left.action.format() < right.action.format();
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                         return left.reactor == right.reactor && left.action == right.action;
                     }),
                     candidates.end());
    auto revision = nextRevision(battle);
    if (!revision) return Result<std::size_t>::failure(revision.status());
    const auto recordedCandidates = candidates;
    reactions->stack.push_back({triggerSequence, depth, std::move(candidates)});
    const auto from = turn->phase;
    turn->phase     = BattlePhase::Reaction;
    turn->revision  = std::move(revision).takeValue();
    emit(battle, from, BattlePhase::Reaction, turn->tick, "reaction.opened");
    BattleCommand command;
    command.kind = BattleCommandKind::OpenReaction;
    command.expectedRevision = expectedRevision;
    command.triggerSequence = triggerSequence;
    command.candidates = recordedCandidates;
    record(battle, std::move(command));
    return Result<std::size_t>::success(depth, Status::success(StatusCode::Applied));
}

Result<ReactionReceipt> BattleSystem::acceptReaction(Battle& battle, SubjectRef reactor, const LogicalId& action) {
    auto turn      = battle.turn();
    const Revision expectedRevision = turn->revision;
    auto reactions = battle.reactions();
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Reaction || reactions->stack.empty())
        return failure<ReactionReceipt>(DiagnosticCode::PreconditionViolation,
                                        "tactics battle has no open reaction window", "reaction");
    ReactionWindow& window = reactions->stack.back();
    const auto candidate = std::find_if(window.candidates.begin(), window.candidates.end(), [&](const auto& value) {
        return value.reactor == reactor && value.action == action;
    });
    if (candidate == window.candidates.end())
        return failure<ReactionReceipt>(DiagnosticCode::NotFound,
                                        "tactics reaction is not eligible in the top window", "reaction.candidate");
    const std::string key = std::to_string(window.triggerSequence) + ":" + reactor.format() + ":" + action.format();
    if (std::find(reactions->seen.begin(), reactions->seen.end(), key) != reactions->seen.end())
        return failure<ReactionReceipt>(DiagnosticCode::Conflict,
                                        "tactics reaction already fired for this trigger chain", "reaction.cycle");

    TacticalUnit* reactingUnit = nullptr;
    for (const auto& handle : turn->units) {
        TacticalUnit* unit = resolve<TacticalUnit>(handle);
        if (unit != nullptr && unit->identity()->subject == reactor) {
            reactingUnit = unit;
            break;
        }
    }
    if (reactingUnit == nullptr)
        return failure<ReactionReceipt>(DiagnosticCode::NotFound,
                                        "tactics reacting unit is not part of the battle", "reaction.reactor");
    if (!reactingUnit->turn()->alive || reactingUnit->turn()->reactionPoints <= 0)
        return failure<ReactionReceipt>(DiagnosticCode::PreconditionViolation,
                                        "tactics reacting unit has no available reaction point", "reaction.resource");
    auto revision = nextRevision(battle);
    if (!revision) return Result<ReactionReceipt>::failure(revision.status());

    --reactingUnit->turn()->reactionPoints;
    reactions->seen.push_back(key);
    const std::uint64_t trigger = window.triggerSequence;
    reactions->stack.pop_back();
    turn->phase = reactions->stack.empty() ? BattlePhase::Acting : BattlePhase::Reaction;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Reaction, turn->phase, turn->tick, "reaction.accepted", reactor);
    BattleCommand command;
    command.kind = BattleCommandKind::AcceptReaction;
    command.expectedRevision = expectedRevision;
    command.actor = reactor;
    command.action = action;
    command.triggerSequence = trigger;
    record(battle, std::move(command));
    return Result<ReactionReceipt>::success(
        {trigger, reactor, action, reactingUnit->turn()->reactionPoints}, Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::declineReaction(Battle& battle) {
    auto turn      = battle.turn();
    const Revision expectedRevision = turn->revision;
    auto reactions = battle.reactions();
    if (turn->status != BattleStatus::Running || turn->phase != BattlePhase::Reaction || reactions->stack.empty())
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle has no open reaction window",
                       "reaction");
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    const std::uint64_t trigger = reactions->stack.back().triggerSequence;
    reactions->stack.pop_back();
    turn->phase = reactions->stack.empty() ? BattlePhase::Acting : BattlePhase::Reaction;
    turn->revision = std::move(revision).takeValue();
    emit(battle, BattlePhase::Reaction, turn->phase, turn->tick, "reaction.closed");
    BattleCommand command;
    command.kind = BattleCommandKind::DeclineReaction;
    command.expectedRevision = expectedRevision;
    command.triggerSequence = trigger;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::addObjective(Battle& battle, ObjectiveSpec objective) {
    auto turn = battle.turn();
    if (turn->status != BattleStatus::Setup)
        return failure(DiagnosticCode::PreconditionViolation, "tactics objectives can only be added during setup",
                       "battle.status");
    if (!objective.id.isValid() || !objective.beneficiarySide.isValid())
        return failure(DiagnosticCode::InvalidArgument, "tactics objective requires valid identities",
                       "objective");
    const auto sideExists = [&](SubjectRef subject) {
        return std::any_of(turn->sides.begin(), turn->sides.end(), [&](const auto& handle) {
            return sideSubject(handle) == subject;
        });
    };
    if (!sideExists(objective.beneficiarySide))
        return failure(DiagnosticCode::NotFound, "tactics objective beneficiary side is not registered",
                       "objective.beneficiarySide");
    if (objective.kind == ObjectiveKind::EliminateSide &&
        (!objective.targetSide.isValid() || !sideExists(objective.targetSide)))
        return failure(DiagnosticCode::NotFound, "tactics elimination target side is not registered",
                       "objective.targetSide");
    if (objective.kind == ObjectiveKind::SurviveRounds && objective.requiredRound == 0)
        return failure(DiagnosticCode::InvalidArgument, "survive-rounds objective requires a positive round",
                       "objective.requiredRound");
    if (objective.kind == ObjectiveKind::OccupyCells) {
        if (objective.requiredCells.empty())
            return failure(DiagnosticCode::InvalidArgument, "occupy-cells objective requires cells",
                           "objective.requiredCells");
        std::sort(objective.requiredCells.begin(), objective.requiredCells.end());
        objective.requiredCells.erase(std::unique(objective.requiredCells.begin(), objective.requiredCells.end()),
                                      objective.requiredCells.end());
        for (const Cell cell : objective.requiredCells)
            if (!battle.board()->value.contains(cell))
                return failure(DiagnosticCode::NotFound, "objective cell does not exist on the board",
                               "objective.requiredCells");
    }
    const auto duplicate = std::find_if(battle.objectives()->values.begin(), battle.objectives()->values.end(),
                                        [&](const auto& value) { return value.spec.id == objective.id; });
    if (duplicate != battle.objectives()->values.end())
        return failure(DiagnosticCode::Conflict, "tactics objective ID already exists", "objective.id");
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    battle.objectives()->values.push_back({std::move(objective), ObjectiveStatus::Pending, {}});
    turn->revision = std::move(revision).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> BattleSystem::defeatUnit(Battle& battle, SubjectRef subject) {
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    if (turn->status != BattleStatus::Running)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle is not accepting outcomes",
                       "battle.status");
    TacticalUnit* target = nullptr;
    for (const auto& handle : turn->units) {
        auto* unit = resolve<TacticalUnit>(handle);
        if (unit && unit->identity()->subject == subject) {
            target = unit;
            break;
        }
    }
    if (!target) return failure(DiagnosticCode::NotFound, "tactics defeat target is not in the battle", "unit");
    if (!target->turn()->alive)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    auto revision = nextRevision(battle);
    if (!revision) return Result<void>::failure(revision.status());
    auto removed = battle.board()->value.remove(subject);
    if (!removed) return Result<void>::failure(removed.status());
    target->turn()->alive = false;
    target->position()->placed = false;
    turn->revision = std::move(revision).takeValue();
    emit(battle, turn->phase, turn->phase, turn->tick, "unit.defeated", subject);
    if (evaluateObjectives(battle, turn->revision)) {
        const BattlePhase from = turn->phase;
        turn->status = BattleStatus::Ended;
        turn->phase = BattlePhase::BattleEnd;
        emit(battle, from, BattlePhase::BattleEnd, turn->tick, "battle.objective_completed",
             battle.identity()->subject);
    }
    BattleCommand command;
    command.kind = BattleCommandKind::DefeatUnit;
    command.expectedRevision = expectedRevision;
    command.actor = subject;
    record(battle, std::move(command));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::uint64_t> BattleSystem::roll(Battle& battle, const LogicalId& stream) {
    auto turn = battle.turn();
    const Revision expectedRevision = turn->revision;
    if (turn->status != BattleStatus::Running)
        return failure<std::uint64_t>(DiagnosticCode::PreconditionViolation,
                                      "tactics random streams require a running battle", "battle.status");
    if (!stream.isValid())
        return failure<std::uint64_t>(DiagnosticCode::InvalidArgument,
                                      "tactics random stream requires a logical ID", "stream");
    auto revision = nextRevision(battle);
    if (!revision) return Result<std::uint64_t>::failure(revision.status());
    auto [iterator, inserted] = battle.random()->streams.try_emplace(
        stream.format(), Battle::RandomStreamState{initialStreamState(battle.identity()->seed, stream.format()), 0});
    (void)inserted;
    const std::uint64_t value = splitMix64(iterator->second.state);
    ++iterator->second.rollIndex;
    turn->revision = std::move(revision).takeValue();
    emit(battle, turn->phase, turn->phase, turn->tick, "random.rolled", battle.identity()->subject);
    BattleCommand command;
    command.kind = BattleCommandKind::RollRandom;
    command.expectedRevision = expectedRevision;
    command.action = stream;
    record(battle, std::move(command));
    return Result<std::uint64_t>::success(value, Status::success(StatusCode::Applied));
}

Result<std::vector<ecs::EntityHandle>> BattleSystem::orderedUnits(Battle& battle) {
    std::vector<ecs::EntityHandle> result;
    for (const auto& handle : battle.turn()->units) {
        TacticalUnit* unit = resolve<TacticalUnit>(handle);
        if (unit == nullptr)
            return failure<std::vector<ecs::EntityHandle>>(DiagnosticCode::StaleHandle,
                                                           "tactics battle contains a stale unit", "battle.units");
        if (unit->turn()->alive) result.push_back(handle);
    }
    const auto sideIndex = [&](const ecs::EntityHandle& side) {
        const auto& sides = battle.turn()->sides;
        const auto  found = std::find_if(sides.begin(), sides.end(),
                                        [&](const auto& value) { return sameHandle(value, side); });
        return static_cast<std::size_t>(std::distance(sides.begin(), found));
    };
    std::sort(result.begin(), result.end(), [&](const auto& leftHandle, const auto& rightHandle) {
        TacticalUnit* left  = resolve<TacticalUnit>(leftHandle);
        TacticalUnit* right = resolve<TacticalUnit>(rightHandle);
        if (battle.turn()->policy == TurnPolicyKind::Initiative &&
            left->turn()->initiative != right->turn()->initiative)
            return left->turn()->initiative > right->turn()->initiative;
        if (battle.turn()->policy == TurnPolicyKind::SideAlternating) {
            const auto leftSide  = sideIndex(left->membership()->side);
            const auto rightSide = sideIndex(right->membership()->side);
            if (leftSide != rightSide) return leftSide < rightSide;
        }
        return left->identity()->subject.format() < right->identity()->subject.format();
    });
    return Result<std::vector<ecs::EntityHandle>>::success(std::move(result));
}

}  // namespace eve::tactics
