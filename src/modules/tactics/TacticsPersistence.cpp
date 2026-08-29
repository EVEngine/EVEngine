#include "tactics/TacticsPersistence.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <limits>
#include <set>
#include <utility>

namespace eve::tactics {
namespace {

constexpr std::string_view kType = "tactics.battle";

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId schema() {
    const auto value = LogicalId::parse("tactics:battle");
    if (!value) std::terminate();
    return *value;
}

Value cellValue(Cell cell) {
    return Value(Value::Object{{"layer", Value(cell.layer)}, {"x", Value(cell.x)}, {"y", Value(cell.y)}});
}

Value subjectValue(SubjectRef subject) { return Value(subject.isValid() ? subject.format() : std::string{}); }

Result<const Value::Object*> object(const Value& value, std::string path) {
    const auto* result = value.getIf<Value::Object>();
    if (!result) return fail<const Value::Object*>(DiagnosticCode::ParseError, "expected object", std::move(path));
    return Result<const Value::Object*>::success(result);
}

Result<const Value*> field(const Value::Object& value, std::string_view name, std::string path) {
    const auto found = value.find(std::string(name));
    if (found == value.end())
        return fail<const Value*>(DiagnosticCode::ParseError, "missing required field", path + "." + std::string(name));
    return Result<const Value*>::success(&found->second);
}

Result<std::int64_t> integer(const Value& value, std::string path) {
    const auto* result = value.getIf<std::int64_t>();
    if (!result) return fail<std::int64_t>(DiagnosticCode::ParseError, "expected integer", std::move(path));
    return Result<std::int64_t>::success(*result);
}

Result<std::uint64_t> decimal(const Value& value, std::string path) {
    const auto* text = value.getIf<std::string>();
    if (!text) return fail<std::uint64_t>(DiagnosticCode::ParseError, "expected decimal string", std::move(path));
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || end != text->data() + text->size())
        return fail<std::uint64_t>(DiagnosticCode::ParseError, "invalid decimal string", std::move(path));
    return Result<std::uint64_t>::success(result);
}

Result<std::int64_t> signedDecimal(const Value& value, std::string path) {
    const auto* text = value.getIf<std::string>();
    if (!text) return fail<std::int64_t>(DiagnosticCode::ParseError, "expected signed decimal string", path);
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || end != text->data() + text->size())
        return fail<std::int64_t>(DiagnosticCode::ParseError, "invalid signed decimal string", std::move(path));
    return Result<std::int64_t>::success(result);
}

Result<SubjectRef> subject(const Value& value, std::string path, bool allowNil = false) {
    const auto* text = value.getIf<std::string>();
    if (!text) return fail<SubjectRef>(DiagnosticCode::ParseError, "expected subject UUID", std::move(path));
    if (allowNil && text->empty()) return Result<SubjectRef>::success(SubjectRef::nil());
    const auto id = PersistentId::parse(*text);
    if (!id || id->isNil())
        return fail<SubjectRef>(DiagnosticCode::ParseError, "invalid subject UUID", std::move(path));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*id));
}

Result<LogicalId> logicalId(const Value& value, std::string path) {
    const auto* text = value.getIf<std::string>();
    if (!text) return fail<LogicalId>(DiagnosticCode::ParseError, "expected logical ID", std::move(path));
    const auto result = LogicalId::parse(*text);
    if (!result) return fail<LogicalId>(DiagnosticCode::ParseError, "invalid logical ID", std::move(path));
    return Result<LogicalId>::success(*result);
}

Result<Cell> parseCell(const Value& value, std::string path) {
    auto candidate = object(value, path);
    if (!candidate) return Result<Cell>::failure(candidate.status());
    if (candidate.value()->size() != 3)
        return fail<Cell>(DiagnosticCode::ParseError, "cell has unknown or missing fields", std::move(path));
    Cell result;
    for (const auto& [name, target] : {std::pair{"x", &result.x}, {"y", &result.y}, {"layer", &result.layer}}) {
        auto member = field(*candidate.value(), name, path);
        if (!member) return Result<Cell>::failure(member.status());
        auto parsed = integer(*member.value(), path + "." + name);
        if (!parsed) return Result<Cell>::failure(parsed.status());
        *target = static_cast<int>(parsed.value());
        if (static_cast<std::int64_t>(*target) != parsed.value())
            return fail<Cell>(DiagnosticCode::ParseError, "cell coordinate is out of range", path + "." + name);
    }
    return Result<Cell>::success(result);
}

template <class T>
T* resolve(const ecs::EntityHandle& handle) noexcept {
    return dynamic_cast<T*>(ecs::try_get(handle));
}

struct UnitCandidate {
    TacticalUnit* unit = nullptr;
    ecs::EntityHandle side{};
    LogicalId     definition;
    Cell          cell;
    int           facing = 0;
    bool          placed = false;
    TacticalUnit::TurnResources turn;
};

struct Candidate {
    BoardState                        board;
    std::uint64_t                    seed = 0;
    BattleStatus                     status = BattleStatus::Setup;
    BattlePhase                      phase = BattlePhase::Setup;
    TurnPolicyKind                   policy = TurnPolicyKind::SideAlternating;
    SimulationTick                   tick;
    std::uint64_t                    round = 0;
    std::size_t                      cursor = 0;
    std::optional<ecs::EntityHandle> activeUnit;
    std::optional<ecs::EntityHandle> activeSide;
    std::vector<UnitCandidate>       units;
    std::vector<ecs::EntityHandle>   sides;
    Battle::Events                   events;
    Battle::Reactions                reactions;
    Battle::Commands                 commands;
    Battle::Objectives               objectives;
    Battle::Random                   random;
};

Value eventValue(const BattleEvent& event) {
    return Value(Value::Object{{"causationCommand", Value(std::to_string(event.causationCommand))},
                               {"correlationCommand", Value(std::to_string(event.correlationCommand))},
                               {"from", Value(static_cast<int>(event.from))},
                               {"sequence", Value(std::to_string(event.sequence))},
                               {"subject", subjectValue(event.subject)},
                               {"tick", Value(std::to_string(event.tick.value()))},
                               {"to", Value(static_cast<int>(event.to))},
                               {"type", Value(event.type)}});
}

Value candidateValue(const ReactionCandidate& candidate) {
    return Value(Value::Object{{"action", Value(candidate.action.format())},
                               {"initiative", Value(candidate.initiative)},
                               {"priority", Value(candidate.priority)},
                               {"reactor", subjectValue(candidate.reactor)}});
}

Value windowValue(const ReactionWindow& window) {
    Value::Array candidates;
    for (const auto& candidate : window.candidates) candidates.push_back(candidateValue(candidate));
    return Value(Value::Object{{"candidates", Value(std::move(candidates))},
                               {"depth", Value(std::to_string(window.depth))},
                               {"triggerSequence", Value(std::to_string(window.triggerSequence))}});
}

Value commandValue(const BattleCommand& command) {
    Value::Array candidates;
    for (const auto& candidate : command.candidates) candidates.push_back(candidateValue(candidate));
    return Value(Value::Object{
        {"action", Value(command.action.isValid() ? command.action.format() : std::string{})},
        {"actor", subjectValue(command.actor)},
        {"candidates", Value(std::move(candidates))},
        {"cell", cellValue(command.cell)},
        {"deltaNanoseconds", Value(std::to_string(command.step.delta.nanoseconds()))},
        {"expectedRevision", Value(std::to_string(command.expectedRevision.value()))},
        {"facing", Value(command.facing)},
        {"kind", Value(static_cast<int>(command.kind))},
        {"policy", Value(static_cast<int>(command.policy))},
        {"resultingRevision", Value(std::to_string(command.resultingRevision.value()))},
        {"sequence", Value(std::to_string(command.sequence))},
        {"tick", Value(std::to_string(command.step.tick.value()))},
        {"triggerSequence", Value(std::to_string(command.triggerSequence))},
    });
}

Value objectiveValue(const ObjectiveState& objective) {
    Value::Array cells;
    for (const Cell cell : objective.spec.requiredCells) cells.push_back(cellValue(cell));
    return Value(Value::Object{
        {"beneficiarySide", subjectValue(objective.spec.beneficiarySide)},
        {"completedRevision", Value(std::to_string(objective.completedRevision.value()))},
        {"endsBattle", Value(objective.spec.endsBattle)},
        {"id", Value(objective.spec.id.format())},
        {"kind", Value(static_cast<int>(objective.spec.kind))},
        {"requiredCells", Value(std::move(cells))},
        {"requiredRound", Value(std::to_string(objective.spec.requiredRound))},
        {"status", Value(static_cast<int>(objective.status))},
        {"targetSide", subjectValue(objective.spec.targetSide)},
    });
}

Value randomValue(const Battle::Random& random) {
    Value::Object streams;
    for (const auto& [name, state] : random.streams)
        streams.emplace(name, Value(Value::Object{{"rollIndex", Value(std::to_string(state.rollIndex))},
                                                   {"state", Value(std::to_string(state.state))}}));
    return Value(std::move(streams));
}

Result<Value> unitValue(TacticalUnit& unit) {
    const auto turn = unit.turn();
    auto* side = resolve<TacticalSide>(unit.membership()->side);
    if (!side)
        return fail<Value>(DiagnosticCode::StaleHandle, "unit belongs to a stale tactical side");
    return Result<Value>::success(Value(Value::Object{
        {"acted", Value(turn->acted)},
        {"actionPoints", Value(turn->actionPoints)},
        {"alive", Value(turn->alive)},
        {"cell", cellValue(unit.position()->cell)},
        {"definition", Value(unit.identity()->definition.isValid() ? unit.identity()->definition.format()
                                                                      : std::string{})},
        {"facing", Value(unit.position()->facing)},
        {"initiative", Value(turn->initiative)},
        {"movePoints", Value(turn->movePoints)},
        {"placed", Value(unit.position()->placed)},
        {"reactionPoints", Value(turn->reactionPoints)},
        {"roundActionPoints", Value(turn->roundActionPoints)},
        {"roundMovePoints", Value(turn->roundMovePoints)},
        {"roundReactionPoints", Value(turn->roundReactionPoints)},
        {"side", subjectValue(side->identity()->subject)},
        {"subject", subjectValue(unit.identity()->subject)},
    }));
}

Result<int> intField(const Value::Object& value, std::string_view name, const std::string& path) {
    auto member = field(value, name, path);
    if (!member) return Result<int>::failure(member.status());
    auto parsed = integer(*member.value(), path + "." + std::string(name));
    if (!parsed) return Result<int>::failure(parsed.status());
    const int result = static_cast<int>(parsed.value());
    if (static_cast<std::int64_t>(result) != parsed.value())
        return fail<int>(DiagnosticCode::ParseError, "integer is out of range", path + "." + std::string(name));
    return Result<int>::success(result);
}

Result<Battle::Events> parseEvents(const Value& value) {
    auto root = object(value, "payload.events");
    if (!root || root.value()->size() != 2)
        return fail<Battle::Events>(DiagnosticCode::ParseError, "invalid events object", "payload.events");
    auto nextMember = field(*root.value(), "nextSequence", "payload.events");
    auto valuesMember = field(*root.value(), "values", "payload.events");
    if (!nextMember || !valuesMember)
        return fail<Battle::Events>(DiagnosticCode::ParseError, "incomplete events object", "payload.events");
    auto next = decimal(*nextMember.value(), "payload.events.nextSequence");
    const auto* values = valuesMember.value()->getIf<Value::Array>();
    if (!next || !values || next.value() == 0)
        return fail<Battle::Events>(DiagnosticCode::ParseError, "invalid event sequence", "payload.events");

    Battle::Events result;
    result.nextSequence = next.value();
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < values->size(); ++i) {
        const std::string path = "payload.events.values[" + std::to_string(i) + "]";
        auto record = object((*values)[i], path);
        if (!record || record.value()->size() != 8)
            return fail<Battle::Events>(DiagnosticCode::ParseError, "invalid event record", path);
        auto sequenceMember = field(*record.value(), "sequence", path);
        auto tickMember = field(*record.value(), "tick", path);
        auto subjectMember = field(*record.value(), "subject", path);
        auto typeMember = field(*record.value(), "type", path);
        auto causationMember = field(*record.value(), "causationCommand", path);
        auto correlationMember = field(*record.value(), "correlationCommand", path);
        auto from = intField(*record.value(), "from", path);
        auto to = intField(*record.value(), "to", path);
        if (!sequenceMember || !tickMember || !subjectMember || !typeMember || !causationMember ||
            !correlationMember || !from || !to ||
            from.value() < 0 || from.value() > static_cast<int>(BattlePhase::BattleEnd) || to.value() < 0 ||
            to.value() > static_cast<int>(BattlePhase::BattleEnd))
            return fail<Battle::Events>(DiagnosticCode::ParseError, "invalid event fields", path);
        auto sequence = decimal(*sequenceMember.value(), path + ".sequence");
        auto tick = decimal(*tickMember.value(), path + ".tick");
        auto subjectRef = subject(*subjectMember.value(), path + ".subject", true);
        auto causation = decimal(*causationMember.value(), path + ".causationCommand");
        auto correlation = decimal(*correlationMember.value(), path + ".correlationCommand");
        const auto* type = typeMember.value()->getIf<std::string>();
        if (!sequence || !tick || !subjectRef || !causation || !correlation || !type || type->empty() ||
            causation.value() == 0 || correlation.value() == 0 || sequence.value() <= previous ||
            sequence.value() >= result.nextSequence)
            return fail<Battle::Events>(DiagnosticCode::InvariantViolation, "invalid event ordering", path);
        previous = sequence.value();
        result.values.push_back({sequence.value(), causation.value(), correlation.value(),
                                 static_cast<BattlePhase>(from.value()), static_cast<BattlePhase>(to.value()),
                                 SimulationTick(tick.value()), *type, subjectRef.value()});
    }
    return Result<Battle::Events>::success(std::move(result));
}

Result<Battle::Reactions> parseReactions(const Value& value, BattlePhase phase) {
    auto root = object(value, "payload.reactions");
    if (!root || root.value()->size() != 3)
        return fail<Battle::Reactions>(DiagnosticCode::ParseError, "invalid reactions object", "payload.reactions");
    auto maxDepthMember = field(*root.value(), "maxDepth", "payload.reactions");
    auto seenMember = field(*root.value(), "seen", "payload.reactions");
    auto stackMember = field(*root.value(), "stack", "payload.reactions");
    if (!maxDepthMember || !seenMember || !stackMember)
        return fail<Battle::Reactions>(DiagnosticCode::ParseError, "incomplete reactions object",
                                       "payload.reactions");
    auto maxDepth = decimal(*maxDepthMember.value(), "payload.reactions.maxDepth");
    const auto* seen = seenMember.value()->getIf<Value::Array>();
    const auto* stack = stackMember.value()->getIf<Value::Array>();
    if (!maxDepth || maxDepth.value() == 0 || maxDepth.value() > static_cast<std::uint64_t>(SIZE_MAX) || !seen ||
        !stack || stack->size() > maxDepth.value())
        return fail<Battle::Reactions>(DiagnosticCode::ParseError, "invalid reaction limits", "payload.reactions");

    Battle::Reactions result;
    result.maxDepth = static_cast<std::size_t>(maxDepth.value());
    std::set<std::string> uniqueSeen;
    for (std::size_t i = 0; i < seen->size(); ++i) {
        const auto* key = (*seen)[i].getIf<std::string>();
        if (!key || key->empty() || !uniqueSeen.insert(*key).second)
            return fail<Battle::Reactions>(DiagnosticCode::InvariantViolation, "invalid reaction cycle guard",
                                           "payload.reactions.seen[" + std::to_string(i) + "]");
        result.seen.push_back(*key);
    }
    for (std::size_t i = 0; i < stack->size(); ++i) {
        const std::string path = "payload.reactions.stack[" + std::to_string(i) + "]";
        auto window = object((*stack)[i], path);
        if (!window || window.value()->size() != 3)
            return fail<Battle::Reactions>(DiagnosticCode::ParseError, "invalid reaction window", path);
        auto triggerMember = field(*window.value(), "triggerSequence", path);
        auto depthMember = field(*window.value(), "depth", path);
        auto candidatesMember = field(*window.value(), "candidates", path);
        if (!triggerMember || !depthMember || !candidatesMember)
            return fail<Battle::Reactions>(DiagnosticCode::ParseError, "incomplete reaction window", path);
        auto trigger = decimal(*triggerMember.value(), path + ".triggerSequence");
        auto depth = decimal(*depthMember.value(), path + ".depth");
        const auto* candidates = candidatesMember.value()->getIf<Value::Array>();
        if (!trigger || trigger.value() == 0 || !depth || depth.value() != i + 1 || !candidates ||
            candidates->empty())
            return fail<Battle::Reactions>(DiagnosticCode::InvariantViolation, "invalid reaction window state",
                                           path);
        ReactionWindow restored{trigger.value(), static_cast<std::size_t>(depth.value()), {}};
        std::set<std::string> uniqueCandidates;
        for (std::size_t j = 0; j < candidates->size(); ++j) {
            const std::string candidatePath = path + ".candidates[" + std::to_string(j) + "]";
            auto candidate = object((*candidates)[j], candidatePath);
            if (!candidate || candidate.value()->size() != 4)
                return fail<Battle::Reactions>(DiagnosticCode::ParseError, "invalid reaction candidate",
                                               candidatePath);
            auto reactorMember = field(*candidate.value(), "reactor", candidatePath);
            auto actionMember = field(*candidate.value(), "action", candidatePath);
            auto priority = intField(*candidate.value(), "priority", candidatePath);
            auto initiative = intField(*candidate.value(), "initiative", candidatePath);
            if (!reactorMember || !actionMember || !priority || !initiative)
                return fail<Battle::Reactions>(DiagnosticCode::ParseError, "incomplete reaction candidate",
                                               candidatePath);
            auto reactor = subject(*reactorMember.value(), candidatePath + ".reactor");
            auto action = logicalId(*actionMember.value(), candidatePath + ".action");
            if (!reactor || !action)
                return fail<Battle::Reactions>(DiagnosticCode::ParseError, "invalid reaction identity",
                                               candidatePath);
            const std::string key = reactor.value().format() + ":" + action.value().format();
            if (!uniqueCandidates.insert(key).second)
                return fail<Battle::Reactions>(DiagnosticCode::Conflict, "duplicate reaction candidate",
                                               candidatePath);
            restored.candidates.push_back(
                {reactor.value(), action.value(), priority.value(), initiative.value()});
        }
        result.stack.push_back(std::move(restored));
    }
    if ((phase == BattlePhase::Reaction) != !result.stack.empty())
        return fail<Battle::Reactions>(DiagnosticCode::InvariantViolation,
                                       "reaction phase and stack disagree", "payload.reactions.stack");
    return Result<Battle::Reactions>::success(std::move(result));
}

Result<Battle::Commands> parseCommands(const Value& value, Revision snapshotRevision) {
    auto root = object(value, "payload.commands");
    if (!root || root.value()->size() != 2)
        return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid commands object", "payload.commands");
    auto nextMember = field(*root.value(), "nextSequence", "payload.commands");
    auto valuesMember = field(*root.value(), "values", "payload.commands");
    if (!nextMember || !valuesMember)
        return fail<Battle::Commands>(DiagnosticCode::ParseError, "incomplete commands object", "payload.commands");
    auto next = decimal(*nextMember.value(), "payload.commands.nextSequence");
    const auto* values = valuesMember.value()->getIf<Value::Array>();
    if (!next || next.value() == 0 || !values)
        return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command sequence", "payload.commands");

    Battle::Commands result;
    result.nextSequence = next.value();
    std::uint64_t previousSequence = 0;
    Revision previousRevision;
    for (std::size_t i = 0; i < values->size(); ++i) {
        const std::string path = "payload.commands.values[" + std::to_string(i) + "]";
        auto record = object((*values)[i], path);
        if (!record || record.value()->size() != 13)
            return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command record", path);
        auto sequenceMember = field(*record.value(), "sequence", path);
        auto expectedMember = field(*record.value(), "expectedRevision", path);
        auto resultingMember = field(*record.value(), "resultingRevision", path);
        auto tickMember = field(*record.value(), "tick", path);
        auto deltaMember = field(*record.value(), "deltaNanoseconds", path);
        auto triggerMember = field(*record.value(), "triggerSequence", path);
        auto actorMember = field(*record.value(), "actor", path);
        auto actionMember = field(*record.value(), "action", path);
        auto cellMember = field(*record.value(), "cell", path);
        auto candidatesMember = field(*record.value(), "candidates", path);
        auto kind = intField(*record.value(), "kind", path);
        auto facing = intField(*record.value(), "facing", path);
        auto policy = intField(*record.value(), "policy", path);
        if (!sequenceMember || !expectedMember || !resultingMember || !tickMember || !deltaMember ||
            !triggerMember || !actorMember || !actionMember || !cellMember || !candidatesMember || !kind ||
            !facing || !policy || kind.value() < 0 ||
            kind.value() > static_cast<int>(BattleCommandKind::RollRandom) || policy.value() < 0 ||
            policy.value() > static_cast<int>(TurnPolicyKind::Initiative))
            return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command fields", path);
        auto sequence = decimal(*sequenceMember.value(), path + ".sequence");
        auto expected = decimal(*expectedMember.value(), path + ".expectedRevision");
        auto resulting = decimal(*resultingMember.value(), path + ".resultingRevision");
        auto tick = decimal(*tickMember.value(), path + ".tick");
        auto delta = signedDecimal(*deltaMember.value(), path + ".deltaNanoseconds");
        auto trigger = decimal(*triggerMember.value(), path + ".triggerSequence");
        auto actor = subject(*actorMember.value(), path + ".actor", true);
        auto cell = parseCell(*cellMember.value(), path + ".cell");
        const auto* actionText = actionMember.value()->getIf<std::string>();
        const auto* candidates = candidatesMember.value()->getIf<Value::Array>();
        if (!sequence || !expected || !resulting || !tick || !delta || !trigger || !actor || !cell ||
            !actionText || !candidates || sequence.value() <= previousSequence ||
            sequence.value() >= result.nextSequence || expected.value() == std::numeric_limits<std::uint64_t>::max() ||
            resulting.value() != expected.value() + 1 ||
            (i > 0 && expected.value() != previousRevision.value()))
            return fail<Battle::Commands>(DiagnosticCode::InvariantViolation, "invalid command ordering", path);
        BattleCommand command;
        command.sequence = sequence.value();
        command.kind = static_cast<BattleCommandKind>(kind.value());
        command.expectedRevision = Revision(expected.value());
        command.resultingRevision = Revision(resulting.value());
        command.step = {SimulationTick(tick.value()), Duration::fromNanoseconds(delta.value())};
        command.actor = actor.value();
        command.cell = cell.value();
        command.facing = facing.value();
        command.policy = static_cast<TurnPolicyKind>(policy.value());
        command.triggerSequence = trigger.value();
        if (!actionText->empty()) {
            const auto parsedAction = LogicalId::parse(*actionText);
            if (!parsedAction)
                return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command action", path + ".action");
            command.action = *parsedAction;
        }
        for (std::size_t j = 0; j < candidates->size(); ++j) {
            const std::string candidatePath = path + ".candidates[" + std::to_string(j) + "]";
            auto candidate = object((*candidates)[j], candidatePath);
            if (!candidate || candidate.value()->size() != 4)
                return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command candidate",
                                              candidatePath);
            auto reactorMember = field(*candidate.value(), "reactor", candidatePath);
            auto candidateActionMember = field(*candidate.value(), "action", candidatePath);
            auto priority = intField(*candidate.value(), "priority", candidatePath);
            auto initiative = intField(*candidate.value(), "initiative", candidatePath);
            if (!reactorMember || !candidateActionMember || !priority || !initiative)
                return fail<Battle::Commands>(DiagnosticCode::ParseError, "incomplete command candidate",
                                              candidatePath);
            auto reactor = subject(*reactorMember.value(), candidatePath + ".reactor");
            auto candidateAction = logicalId(*candidateActionMember.value(), candidatePath + ".action");
            if (!reactor || !candidateAction)
                return fail<Battle::Commands>(DiagnosticCode::ParseError, "invalid command candidate identity",
                                              candidatePath);
            command.candidates.push_back(
                {reactor.value(), candidateAction.value(), priority.value(), initiative.value()});
        }
        const auto invalidCommand = [&]() {
            switch (command.kind) {
                case BattleCommandKind::Start: return false;
                case BattleCommandKind::Advance: return command.step.tick.isZero() || command.step.delta.nanoseconds() < 0;
                case BattleCommandKind::Move:
                case BattleCommandKind::Face:
                case BattleCommandKind::Wait:
                case BattleCommandKind::EndTurn:
                case BattleCommandKind::DefeatUnit: return !command.actor.isValid();
                case BattleCommandKind::Finish: return false;
                case BattleCommandKind::OpenReaction:
                    return command.triggerSequence == 0 || command.candidates.empty();
                case BattleCommandKind::AcceptReaction:
                    return command.triggerSequence == 0 || !command.actor.isValid() || !command.action.isValid();
                case BattleCommandKind::DeclineReaction: return command.triggerSequence == 0;
                case BattleCommandKind::RollRandom: return !command.action.isValid();
            }
            return true;
        };
        if (invalidCommand())
            return fail<Battle::Commands>(DiagnosticCode::InvariantViolation,
                                          "command fields disagree with command kind", path);
        previousSequence = sequence.value();
        previousRevision = command.resultingRevision;
        result.values.push_back(std::move(command));
    }
    if (!result.values.empty() && result.values.back().resultingRevision != snapshotRevision)
        return fail<Battle::Commands>(DiagnosticCode::Conflict,
                                      "command log revision differs from snapshot revision", "payload.commands");
    return Result<Battle::Commands>::success(std::move(result));
}

Result<Battle::Objectives> parseObjectives(Battle& battle, const BoardState& board, const Value& value,
                                            Revision snapshotRevision) {
    const auto* values = value.getIf<Value::Array>();
    if (!values)
        return fail<Battle::Objectives>(DiagnosticCode::ParseError, "objectives must be an array",
                                        "payload.objectives");
    std::set<std::string> sideSubjects;
    for (const auto& handle : battle.turn()->sides) {
        auto* side = resolve<TacticalSide>(handle);
        if (!side)
            return fail<Battle::Objectives>(DiagnosticCode::StaleHandle, "target battle contains a stale side");
        sideSubjects.insert(side->identity()->subject.format());
    }
    Battle::Objectives result;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < values->size(); ++i) {
        const std::string path = "payload.objectives[" + std::to_string(i) + "]";
        auto record = object((*values)[i], path);
        if (!record || record.value()->size() != 9)
            return fail<Battle::Objectives>(DiagnosticCode::ParseError, "invalid objective record", path);
        auto idMember = field(*record.value(), "id", path);
        auto beneficiaryMember = field(*record.value(), "beneficiarySide", path);
        auto targetMember = field(*record.value(), "targetSide", path);
        auto roundMember = field(*record.value(), "requiredRound", path);
        auto cellsMember = field(*record.value(), "requiredCells", path);
        auto endsMember = field(*record.value(), "endsBattle", path);
        auto completedMember = field(*record.value(), "completedRevision", path);
        auto kind = intField(*record.value(), "kind", path);
        auto status = intField(*record.value(), "status", path);
        if (!idMember || !beneficiaryMember || !targetMember || !roundMember || !cellsMember || !endsMember ||
            !completedMember || !kind || !status || kind.value() < 0 ||
            kind.value() > static_cast<int>(ObjectiveKind::OccupyCells) || status.value() < 0 ||
            status.value() > static_cast<int>(ObjectiveStatus::Completed))
            return fail<Battle::Objectives>(DiagnosticCode::ParseError, "invalid objective fields", path);
        auto id = logicalId(*idMember.value(), path + ".id");
        auto beneficiary = subject(*beneficiaryMember.value(), path + ".beneficiarySide");
        auto target = subject(*targetMember.value(), path + ".targetSide", true);
        auto round = decimal(*roundMember.value(), path + ".requiredRound");
        auto completed = decimal(*completedMember.value(), path + ".completedRevision");
        const auto* cells = cellsMember.value()->getIf<Value::Array>();
        const auto* endsBattle = endsMember.value()->getIf<bool>();
        if (!id || !beneficiary || !target || !round || !completed || !cells || !endsBattle ||
            !ids.insert(id.value().format()).second || !sideSubjects.contains(beneficiary.value().format()) ||
            (target.value().isValid() && !sideSubjects.contains(target.value().format())) ||
            completed.value() > snapshotRevision.value())
            return fail<Battle::Objectives>(DiagnosticCode::InvariantViolation, "invalid objective state", path);
        ObjectiveState objective;
        objective.spec.id = id.value();
        objective.spec.kind = static_cast<ObjectiveKind>(kind.value());
        objective.spec.beneficiarySide = beneficiary.value();
        objective.spec.targetSide = target.value();
        objective.spec.requiredRound = round.value();
        objective.spec.endsBattle = *endsBattle;
        objective.status = static_cast<ObjectiveStatus>(status.value());
        objective.completedRevision = Revision(completed.value());
        for (std::size_t j = 0; j < cells->size(); ++j) {
            auto cell = parseCell((*cells)[j], path + ".requiredCells[" + std::to_string(j) + "]");
            if (!cell || !board.contains(cell.value()))
                return fail<Battle::Objectives>(DiagnosticCode::InvariantViolation, "invalid objective cell", path);
            objective.spec.requiredCells.push_back(cell.value());
        }
        if ((objective.status == ObjectiveStatus::Pending && !objective.completedRevision.isZero()) ||
            (objective.status == ObjectiveStatus::Completed && objective.completedRevision.isZero()) ||
            (objective.spec.kind == ObjectiveKind::EliminateSide && !objective.spec.targetSide.isValid()) ||
            (objective.spec.kind == ObjectiveKind::SurviveRounds && objective.spec.requiredRound == 0) ||
            (objective.spec.kind == ObjectiveKind::OccupyCells && objective.spec.requiredCells.empty()))
            return fail<Battle::Objectives>(DiagnosticCode::InvariantViolation, "inconsistent objective state",
                                            path);
        std::sort(objective.spec.requiredCells.begin(), objective.spec.requiredCells.end());
        if (std::adjacent_find(objective.spec.requiredCells.begin(), objective.spec.requiredCells.end()) !=
            objective.spec.requiredCells.end())
            return fail<Battle::Objectives>(DiagnosticCode::Conflict, "duplicate objective cell", path);
        result.values.push_back(std::move(objective));
    }
    return Result<Battle::Objectives>::success(std::move(result));
}

Result<Battle::Random> parseRandom(const Value& value) {
    const auto* streams = value.getIf<Value::Object>();
    if (!streams)
        return fail<Battle::Random>(DiagnosticCode::ParseError, "random streams must be an object",
                                    "payload.random");
    Battle::Random result;
    for (const auto& [name, stateValue] : *streams) {
        if (!LogicalId::parse(name))
            return fail<Battle::Random>(DiagnosticCode::ParseError, "invalid random stream logical ID",
                                        "payload.random." + name);
        auto state = object(stateValue, "payload.random." + name);
        if (!state || state.value()->size() != 2)
            return fail<Battle::Random>(DiagnosticCode::ParseError, "invalid random stream state",
                                        "payload.random." + name);
        auto stateMember = field(*state.value(), "state", "payload.random." + name);
        auto indexMember = field(*state.value(), "rollIndex", "payload.random." + name);
        if (!stateMember || !indexMember)
            return fail<Battle::Random>(DiagnosticCode::ParseError, "incomplete random stream state",
                                        "payload.random." + name);
        auto parsedState = decimal(*stateMember.value(), "payload.random." + name + ".state");
        auto parsedIndex = decimal(*indexMember.value(), "payload.random." + name + ".rollIndex");
        if (!parsedState || !parsedIndex || parsedIndex.value() == 0)
            return fail<Battle::Random>(DiagnosticCode::InvariantViolation, "invalid random stream state",
                                        "payload.random." + name);
        result.streams.emplace(name, Battle::RandomStreamState{parsedState.value(), parsedIndex.value()});
    }
    return Result<Battle::Random>::success(std::move(result));
}

Result<Candidate> parseCandidate(Battle& battle, const Value& payload, Revision snapshotRevision) {
    auto root = object(payload, "payload");
    if (!root) return Result<Candidate>::failure(root.status());
    static const std::set<std::string> fields = {"activeUnit", "board", "commands", "cursor", "events",
                                                  "objectives", "phase", "policy", "random", "reactions", "round",
                                                  "seed", "sides", "status", "units"};
    for (const auto& [name, unused] : *root.value())
        if (!fields.contains(name)) return fail<Candidate>(DiagnosticCode::ParseError, "unknown payload field", name);
    if (root.value()->size() != fields.size())
        return fail<Candidate>(DiagnosticCode::ParseError, "snapshot payload is incomplete", "payload");

    Candidate result;
    auto status = intField(*root.value(), "status", "payload");
    auto phase = intField(*root.value(), "phase", "payload");
    auto policy = intField(*root.value(), "policy", "payload");
    if (!status || !phase || !policy) return fail<Candidate>(DiagnosticCode::ParseError, "invalid battle enum");
    if (status.value() < 0 || status.value() > static_cast<int>(BattleStatus::Ended) || phase.value() < 0 ||
        phase.value() > static_cast<int>(BattlePhase::BattleEnd) || policy.value() < 0 ||
        policy.value() > static_cast<int>(TurnPolicyKind::Initiative))
        return fail<Candidate>(DiagnosticCode::UnknownVersion, "snapshot contains an unknown battle enum");
    result.status = static_cast<BattleStatus>(status.value());
    result.phase = static_cast<BattlePhase>(phase.value());
    result.policy = static_cast<TurnPolicyKind>(policy.value());
    auto roundValue = field(*root.value(), "round", "payload");
    auto cursorValue = field(*root.value(), "cursor", "payload");
    if (!roundValue || !cursorValue) return fail<Candidate>(DiagnosticCode::ParseError, "missing turn state");
    auto round = decimal(*roundValue.value(), "payload.round");
    auto cursor = decimal(*cursorValue.value(), "payload.cursor");
    if (!round || !cursor || cursor.value() > static_cast<std::uint64_t>(SIZE_MAX))
        return fail<Candidate>(DiagnosticCode::ParseError, "invalid turn counters");
    result.round = round.value();
    result.cursor = static_cast<std::size_t>(cursor.value());
    auto seedMember = field(*root.value(), "seed", "payload");
    if (!seedMember) return Result<Candidate>::failure(seedMember.status());
    auto seed = decimal(*seedMember.value(), "payload.seed");
    if (!seed) return Result<Candidate>::failure(seed.status());
    result.seed = seed.value();

    auto boardMember = field(*root.value(), "board", "payload");
    if (!boardMember) return Result<Candidate>::failure(boardMember.status());
    auto boardObject = object(*boardMember.value(), "payload.board");
    if (!boardObject || boardObject.value()->size() != 2)
        return fail<Candidate>(DiagnosticCode::ParseError, "invalid board object", "payload.board");
    auto topology = intField(*boardObject.value(), "topology", "payload.board");
    auto cellsMember = field(*boardObject.value(), "cells", "payload.board");
    if (!topology || !cellsMember || topology.value() < 0 || topology.value() > 2)
        return fail<Candidate>(DiagnosticCode::ParseError, "invalid board topology", "payload.board.topology");
    const auto* cells = cellsMember.value()->getIf<Value::Array>();
    if (!cells)
        return fail<Candidate>(DiagnosticCode::ParseError, "board cells must be an array", "payload.board.cells");
    result.board.setTopology(static_cast<BoardTopology>(topology.value()));
    for (std::size_t i = 0; i < cells->size(); ++i) {
        const std::string path = "payload.board.cells[" + std::to_string(i) + "]";
        auto record = object((*cells)[i], path);
        if (!record || record.value()->size() != 5)
            return fail<Candidate>(DiagnosticCode::ParseError, "invalid board cell record", path);
        auto cellMember = field(*record.value(), "cell", path);
        auto moveCost = intField(*record.value(), "moveCost", path);
        auto height = intField(*record.value(), "height", path);
        auto passableMember = field(*record.value(), "passable", path);
        auto tagsMember = field(*record.value(), "tags", path);
        if (!cellMember || !moveCost || !height || !passableMember || !tagsMember)
            return fail<Candidate>(DiagnosticCode::ParseError, "incomplete board cell record", path);
        auto cell = parseCell(*cellMember.value(), path + ".cell");
        const auto* passable = passableMember.value()->getIf<bool>();
        const auto* tags = tagsMember.value()->getIf<Value::Array>();
        if (!cell || !passable || !tags) return fail<Candidate>(DiagnosticCode::ParseError, "invalid board cell", path);
        CellState state{moveCost.value(), height.value(), *passable, {}};
        for (const auto& tag : *tags) {
            const auto* text = tag.getIf<std::string>();
            if (!text) return fail<Candidate>(DiagnosticCode::ParseError, "cell tag must be a string", path + ".tags");
            state.tags.push_back(*text);
        }
        auto added = result.board.addCell(cell.value(), std::move(state));
        if (!added) return Result<Candidate>::failure(added.status());
    }

    std::map<std::string, TacticalSide*> currentSides;
    for (const auto& handle : battle.turn()->sides) {
        auto* side = resolve<TacticalSide>(handle);
        if (!side) return fail<Candidate>(DiagnosticCode::StaleHandle, "target battle contains a stale side");
        currentSides.emplace(side->identity()->subject.format(), side);
    }
    auto sidesMember = field(*root.value(), "sides", "payload");
    if (!sidesMember) return Result<Candidate>::failure(sidesMember.status());
    const auto* sides = sidesMember.value()->getIf<Value::Array>();
    if (!sides || sides->size() != currentSides.size())
        return fail<Candidate>(DiagnosticCode::Conflict, "snapshot side set differs from target battle",
                               "payload.sides");
    std::set<std::string> restoredSides;
    for (std::size_t i = 0; i < sides->size(); ++i) {
        auto side = subject((*sides)[i], "payload.sides[" + std::to_string(i) + "]");
        if (!side) return Result<Candidate>::failure(side.status());
        const auto found = currentSides.find(side.value().format());
        if (found == currentSides.end() || !restoredSides.insert(found->first).second)
            return fail<Candidate>(DiagnosticCode::Conflict, "snapshot side cannot resolve uniquely",
                                   "payload.sides[" + std::to_string(i) + "]");
        result.sides.push_back(found->second->identity()->self);
    }

    std::map<std::string, TacticalUnit*> currentUnits;
    for (const auto& handle : battle.turn()->units) {
        auto* unit = resolve<TacticalUnit>(handle);
        if (!unit) return fail<Candidate>(DiagnosticCode::StaleHandle, "target battle contains a stale unit");
        currentUnits.emplace(unit->identity()->subject.format(), unit);
    }
    auto unitsMember = field(*root.value(), "units", "payload");
    if (!unitsMember) return Result<Candidate>::failure(unitsMember.status());
    const auto* units = unitsMember.value()->getIf<Value::Array>();
    if (!units || units->size() != currentUnits.size())
        return fail<Candidate>(DiagnosticCode::Conflict, "snapshot unit set differs from target battle",
                               "payload.units");
    std::set<std::string> restoredSubjects;
    for (std::size_t i = 0; i < units->size(); ++i) {
        const std::string path = "payload.units[" + std::to_string(i) + "]";
        auto unitObject = object((*units)[i], path);
        if (!unitObject || unitObject.value()->size() != 15)
            return fail<Candidate>(DiagnosticCode::ParseError, "invalid unit record", path);
        auto subjectMember = field(*unitObject.value(), "subject", path);
        auto cellMember = field(*unitObject.value(), "cell", path);
        auto definitionMember = field(*unitObject.value(), "definition", path);
        auto sideMember = field(*unitObject.value(), "side", path);
        if (!subjectMember || !cellMember || !definitionMember || !sideMember)
            return fail<Candidate>(DiagnosticCode::ParseError, "incomplete unit record", path);
        auto subjectRef = subject(*subjectMember.value(), path + ".subject");
        auto cell = parseCell(*cellMember.value(), path + ".cell");
        auto facing = intField(*unitObject.value(), "facing", path);
        const auto* definitionText = definitionMember.value()->getIf<std::string>();
        auto sideRef = subject(*sideMember.value(), path + ".side");
        if (!subjectRef || !cell || !facing || !definitionText || !sideRef)
            return fail<Candidate>(DiagnosticCode::ParseError, "invalid unit identity or cell", path);
        const auto found = currentUnits.find(subjectRef.value().format());
        if (found == currentUnits.end() || !restoredSubjects.insert(found->first).second)
            return fail<Candidate>(DiagnosticCode::Conflict, "snapshot unit cannot resolve uniquely",
                                   path + ".subject");
        const auto restoredSide = currentSides.find(sideRef.value().format());
        if (restoredSide == currentSides.end())
            return fail<Candidate>(DiagnosticCode::Conflict, "snapshot unit side is absent", path + ".side");
        LogicalId definition;
        if (!definitionText->empty()) {
            const auto parsedDefinition = LogicalId::parse(*definitionText);
            if (!parsedDefinition)
                return fail<Candidate>(DiagnosticCode::ParseError, "invalid unit definition", path + ".definition");
            definition = *parsedDefinition;
        }
        TacticalUnit::TurnResources turn;
        for (const auto& [name, target] :
             {std::pair{"actionPoints", &turn.actionPoints}, {"movePoints", &turn.movePoints},
              {"reactionPoints", &turn.reactionPoints}, {"roundActionPoints", &turn.roundActionPoints},
              {"roundMovePoints", &turn.roundMovePoints},
              {"roundReactionPoints", &turn.roundReactionPoints}, {"initiative", &turn.initiative}}) {
            auto parsed = intField(*unitObject.value(), name, path);
            if (!parsed) return Result<Candidate>::failure(parsed.status());
            *target = parsed.value();
        }
        auto aliveMember = field(*unitObject.value(), "alive", path);
        auto actedMember = field(*unitObject.value(), "acted", path);
        auto placedMember = field(*unitObject.value(), "placed", path);
        const bool* alive = aliveMember ? aliveMember.value()->getIf<bool>() : nullptr;
        const bool* acted = actedMember ? actedMember.value()->getIf<bool>() : nullptr;
        const bool* placed = placedMember ? placedMember.value()->getIf<bool>() : nullptr;
        if (!alive || !acted || !placed)
            return fail<Candidate>(DiagnosticCode::ParseError, "invalid unit flags", path);
        turn.alive = *alive;
        turn.acted = *acted;
        if (*alive != *placed)
            return fail<Candidate>(DiagnosticCode::InvariantViolation,
                                   "v1 tactics units must be placed exactly while alive", path);
        if (*placed) {
            auto placement = result.board.place(subjectRef.value(), cell.value());
            if (!placement) return Result<Candidate>::failure(placement.status());
        }
        const int facingCount = result.board.topology() == BoardTopology::Square4   ? 4
                                : result.board.topology() == BoardTopology::Square8 ? 8
                                                                                    : 6;
        if (facing.value() < 0 || facing.value() >= facingCount)
            return fail<Candidate>(DiagnosticCode::InvariantViolation, "unit facing is invalid", path + ".facing");
        result.units.push_back(
            {found->second, restoredSide->second->identity()->self, definition, cell.value(), facing.value(),
             *placed, turn});
    }
    auto activeMember = field(*root.value(), "activeUnit", "payload");
    if (!activeMember) return Result<Candidate>::failure(activeMember.status());
    auto active = subject(*activeMember.value(), "payload.activeUnit", true);
    if (!active) return Result<Candidate>::failure(active.status());
    if (active.value().isValid()) {
        const auto found = currentUnits.find(active.value().format());
        if (found == currentUnits.end()) return fail<Candidate>(DiagnosticCode::Conflict, "active unit is absent");
        result.activeUnit = found->second->identity()->self;
        result.activeSide = found->second->membership()->side;
    }
    if (result.cursor >= result.units.size() && !result.units.empty())
        return fail<Candidate>(DiagnosticCode::InvariantViolation, "turn cursor is outside unit schedule");
    auto eventsMember = field(*root.value(), "events", "payload");
    auto reactionsMember = field(*root.value(), "reactions", "payload");
    auto commandsMember = field(*root.value(), "commands", "payload");
    auto objectivesMember = field(*root.value(), "objectives", "payload");
    auto randomMember = field(*root.value(), "random", "payload");
    if (!eventsMember || !reactionsMember || !commandsMember || !objectivesMember || !randomMember)
        return fail<Candidate>(DiagnosticCode::ParseError, "snapshot is missing deterministic streams");
    auto events = parseEvents(*eventsMember.value());
    auto reactions = parseReactions(*reactionsMember.value(), result.phase);
    auto commands = parseCommands(*commandsMember.value(), snapshotRevision);
    auto objectives = parseObjectives(battle, result.board, *objectivesMember.value(), snapshotRevision);
    auto random = parseRandom(*randomMember.value());
    if (!events) return Result<Candidate>::failure(events.status());
    if (!reactions) return Result<Candidate>::failure(reactions.status());
    if (!commands) return Result<Candidate>::failure(commands.status());
    if (!objectives) return Result<Candidate>::failure(objectives.status());
    if (!random) return Result<Candidate>::failure(random.status());
    result.events = std::move(events).takeValue();
    result.reactions = std::move(reactions).takeValue();
    result.commands = std::move(commands).takeValue();
    result.objectives = std::move(objectives).takeValue();
    result.random = std::move(random).takeValue();
    std::set<std::uint64_t> commandSequences;
    std::map<std::string, std::uint64_t> randomCommandCounts;
    for (const auto& command : result.commands.values) {
        commandSequences.insert(command.sequence);
        if (command.kind == BattleCommandKind::RollRandom) ++randomCommandCounts[command.action.format()];
    }
    for (const auto& [stream, state] : result.random.streams)
        if (randomCommandCounts[stream] != state.rollIndex)
            return fail<Candidate>(DiagnosticCode::InvariantViolation,
                                   "random stream roll index differs from accepted command log",
                                   "payload.random." + stream);
    for (const auto& event : result.events.values) {
        if (!commandSequences.contains(event.causationCommand) ||
            !commandSequences.contains(event.correlationCommand))
            return fail<Candidate>(DiagnosticCode::InvariantViolation,
                                   "event causation/correlation references an absent command",
                                   "payload.events");
    }
    std::set<std::uint64_t> eventSequences;
    for (const auto& event : result.events.values) eventSequences.insert(event.sequence);
    for (const auto& window : result.reactions.stack) {
        if (!eventSequences.contains(window.triggerSequence))
            return fail<Candidate>(DiagnosticCode::InvariantViolation,
                                   "reaction window references an absent trigger event",
                                   "payload.reactions.stack");
        for (const auto& reactionCandidate : window.candidates)
            if (!currentUnits.contains(reactionCandidate.reactor.format()))
                return fail<Candidate>(DiagnosticCode::Conflict,
                                       "reaction candidate is absent from the target battle",
                                       "payload.reactions.stack");
    }
    auto valid = result.board.validateInvariants();
    if (!valid) return Result<Candidate>::failure(valid.status());
    return Result<Candidate>::success(std::move(result));
}

}  // namespace

Result<SnapshotEnvelope> TacticsPersistence::snapshot(Battle& battle, const SnapshotHashProvider& hashProvider) {
    Value::Array cells;
    for (const auto& record : battle.board()->value.records()) {
        Value::Array tags;
        for (const auto& tag : record.state.tags) tags.emplace_back(tag);
        cells.emplace_back(Value::Object{{"cell", cellValue(record.cell)},
                                         {"height", Value(record.state.height)},
                                         {"moveCost", Value(record.state.moveCost)},
                                         {"passable", Value(record.state.passable)},
                                         {"tags", Value(std::move(tags))}});
    }
    Value::Array units;
    for (const auto& handle : battle.turn()->units) {
        auto* unit = resolve<TacticalUnit>(handle);
        if (!unit) return fail<SnapshotEnvelope>(DiagnosticCode::StaleHandle, "battle contains a stale unit");
        auto encoded = unitValue(*unit);
        if (!encoded) return Result<SnapshotEnvelope>::failure(encoded.status());
        units.push_back(std::move(encoded).takeValue());
    }
    Value::Array sides;
    for (const auto& handle : battle.turn()->sides) {
        auto* side = resolve<TacticalSide>(handle);
        if (!side) return fail<SnapshotEnvelope>(DiagnosticCode::StaleHandle, "battle contains a stale side");
        sides.push_back(subjectValue(side->identity()->subject));
    }
    SubjectRef active;
    if (battle.turn()->activeUnit) {
        auto* unit = resolve<TacticalUnit>(*battle.turn()->activeUnit);
        if (!unit) return fail<SnapshotEnvelope>(DiagnosticCode::StaleHandle, "battle active unit is stale");
        active = unit->identity()->subject;
    }
    Value::Array events;
    for (const auto& event : battle.events()->values) events.push_back(eventValue(event));
    Value::Array reactionStack;
    for (const auto& window : battle.reactions()->stack) reactionStack.push_back(windowValue(window));
    Value::Array reactionSeen;
    for (const auto& key : battle.reactions()->seen) reactionSeen.emplace_back(key);
    Value::Array commands;
    for (const auto& command : battle.commands()->values) commands.push_back(commandValue(command));
    Value::Array objectives;
    for (const auto& objective : battle.objectives()->values) objectives.push_back(objectiveValue(objective));
    Value payload(Value::Object{
        {"activeUnit", subjectValue(active)},
        {"board", Value(Value::Object{{"cells", Value(std::move(cells))},
                                       {"topology", Value(static_cast<int>(battle.board()->value.topology()))}})},
        {"cursor", Value(std::to_string(battle.turn()->cursor))},
        {"commands", Value(Value::Object{{"nextSequence", Value(std::to_string(battle.commands()->nextSequence))},
                                          {"values", Value(std::move(commands))}})},
        {"events", Value(Value::Object{{"nextSequence", Value(std::to_string(battle.events()->nextSequence))},
                                        {"values", Value(std::move(events))}})},
        {"objectives", Value(std::move(objectives))},
        {"phase", Value(static_cast<int>(battle.turn()->phase))},
        {"policy", Value(static_cast<int>(battle.turn()->policy))},
        {"random", randomValue(*battle.random())},
        {"reactions", Value(Value::Object{
                          {"maxDepth", Value(std::to_string(battle.reactions()->maxDepth))},
                          {"seen", Value(std::move(reactionSeen))},
                          {"stack", Value(std::move(reactionStack))}})},
        {"round", Value(std::to_string(battle.turn()->round))},
        {"seed", Value(std::to_string(battle.identity()->seed))},
        {"sides", Value(std::move(sides))},
        {"status", Value(static_cast<int>(battle.turn()->status))},
        {"units", Value(std::move(units))},
    });
    return makeSnapshotEnvelope(std::string(kType), schema(), SchemaVersion(1),
                                battle.identity()->subject.persistentId(), battle.turn()->revision,
                                battle.turn()->tick,
                                std::move(payload), hashProvider);
}

Result<void> TacticsPersistence::restore(Battle& battle, const SnapshotEnvelope& source,
                                          const SnapshotHashProvider& hashProvider) {
    if (source.type != kType || source.schema != schema())
        return fail<void>(DiagnosticCode::InvalidArgument, "snapshot does not belong to tactics battle");
    if (source.instanceId != battle.identity()->subject.persistentId())
        return fail<void>(DiagnosticCode::Conflict, "snapshot battle identity differs from target");
    auto verified = verifySnapshotEnvelope(source, hashProvider);
    if (!verified) return Result<void>::failure(verified.status());
    if (source.schemaVersion != SchemaVersion(1))
        return fail<void>(DiagnosticCode::UnknownVersion, "unsupported tactics battle snapshot version");
    auto metadata = validateSnapshotPayloadMetadata(source.payload, source.revision, source.tick);
    if (!metadata) return Result<void>::failure(metadata.status());
    auto candidate = parseCandidate(battle, source.payload, source.revision);
    if (!candidate) return Result<void>::failure(candidate.status());

    Candidate restored = std::move(candidate).takeValue();
    battle.board()->value = std::move(restored.board);
    battle.identity()->seed = restored.seed;
    for (auto& unit : restored.units) {
        unit.unit->identity()->definition = unit.definition;
        unit.unit->membership()->side = unit.side;
        *unit.unit->turn() = unit.turn;
        unit.unit->position()->cell = unit.cell;
        unit.unit->position()->facing = unit.facing;
        unit.unit->position()->placed = unit.placed;
    }
    auto turn = battle.turn();
    turn->status = restored.status;
    turn->phase = restored.phase;
    turn->policy = restored.policy;
    turn->tick = source.tick;
    turn->revision = source.revision;
    turn->round = restored.round;
    turn->cursor = restored.cursor;
    turn->activeUnit = restored.activeUnit;
    turn->activeSide = restored.activeSide;
    turn->sides = std::move(restored.sides);
    *battle.events() = std::move(restored.events);
    *battle.reactions() = std::move(restored.reactions);
    *battle.commands() = std::move(restored.commands);
    *battle.objectives() = std::move(restored.objectives);
    *battle.random() = std::move(restored.random);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace eve::tactics
