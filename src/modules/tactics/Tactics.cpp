#include "tactics/Tactics.h"

#include "common/SquirrelBinding.h"
#include "common/Capability.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <limits>
#include <utility>

namespace eve::tactics {
namespace {

struct ScriptTacticsBattle {
    explicit ScriptTacticsBattle(TacticsBattleSessionRef value) : reference(value) {}
    ~ScriptTacticsBattle() noexcept {
        Tactics::release(reference).ignore("script tactics battle proxy destruction");
    }
    TacticsBattleSessionRef reference;
};

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<void> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

Result<SubjectRef> bindingSubject(const std::string& text, std::string path) {
    const auto id = PersistentId::parse(text);
    if (!id || id->isNil())
        return failure<SubjectRef>(DiagnosticCode::InvalidArgument, "expected a non-nil canonical UUID",
                                   std::move(path));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*id));
}

Result<LogicalId> bindingLogicalId(const std::string& text, std::string path) {
    const auto id = LogicalId::parse(text);
    if (!id)
        return failure<LogicalId>(DiagnosticCode::InvalidArgument, "expected a namespace:name logical ID",
                                  std::move(path));
    return Result<LogicalId>::success(*id);
}

Result<std::vector<Cell>> bindingCellsJson(const std::string& json) {
    auto value = Value::fromJson(json);
    if (!value) return Result<std::vector<Cell>>::failure(value.status());
    const auto* rows = value.value().getIf<Value::Array>();
    if (!rows || rows->empty())
        return failure<std::vector<Cell>>(DiagnosticCode::InvalidArgument,
                                          "objective cells JSON must be a non-empty array", "cellsJson");
    std::vector<Cell> result;
    result.reserve(rows->size());
    for (std::size_t index = 0; index < rows->size(); ++index) {
        const auto* tuple = (*rows)[index].getIf<Value::Array>();
        if (!tuple || tuple->size() != 3 || !(*tuple)[0].isInt64() || !(*tuple)[1].isInt64() ||
            !(*tuple)[2].isInt64())
            return failure<std::vector<Cell>>(DiagnosticCode::InvalidArgument,
                                              "objective cell must be [x,y,layer] integers",
                                              "cellsJson[" + std::to_string(index) + "]");
        const auto convert = [&](std::size_t coordinate) -> Result<int> {
            const auto value = (*tuple)[coordinate].asInt();
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                return failure<int>(DiagnosticCode::InvalidArgument, "objective cell coordinate is out of range",
                                    "cellsJson[" + std::to_string(index) + "]");
            return Result<int>::success(static_cast<int>(value));
        };
        auto x = convert(0);
        auto y = convert(1);
        auto layer = convert(2);
        if (!x) return Result<std::vector<Cell>>::failure(x.status());
        if (!y) return Result<std::vector<Cell>>::failure(y.status());
        if (!layer) return Result<std::vector<Cell>>::failure(layer.status());
        result.push_back({x.value(), y.value(), layer.value()});
    }
    return Result<std::vector<Cell>>::success(std::move(result));
}

Tactics* currentModule() noexcept { return ModuleManager::getInstance<Tactics>("Tactics"); }

Result<TacticsBattleSession*> bindingSession(ScriptTacticsBattle* proxy) {
    if (!proxy)
        return failure<TacticsBattleSession*>(DiagnosticCode::InvalidArgument,
                                              "tactics battle proxy must not be null", "battle");
    auto session = Tactics::resolve(proxy->reference);
    if (!session.isBound())
        return failure<TacticsBattleSession*>(DiagnosticCode::StaleHandle,
                                              "tactics battle session handle is stale", "battle");
    return Result<TacticsBattleSession*>::success(session.get());
}

Value eventProjection(const BattleEvent& event) {
    return Value(Value::Object{{"causationCommand", Value(std::to_string(event.causationCommand))},
                               {"correlationCommand", Value(std::to_string(event.correlationCommand))},
                               {"from", Value(std::string(phaseName(event.from)))},
                               {"sequence", Value(std::to_string(event.sequence))},
                               {"subject", Value(event.subject.isValid() ? event.subject.format() : std::string{})},
                               {"tick", Value(std::to_string(event.tick.value()))},
                               {"to", Value(std::string(phaseName(event.to)))},
                               {"type", Value(event.type)}});
}

Value bindingCellValue(Cell cell) {
    return Value(Value::Object{{"layer", Value(cell.layer)}, {"x", Value(cell.x)}, {"y", Value(cell.y)}});
}

Result<std::string> scriptAddSide(ScriptTacticsBattle* proxy, const std::string& subjectText) {
    auto session = bindingSession(proxy);
    if (!session) return Result<std::string>::failure(session.status());
    auto subject = bindingSubject(subjectText, "subject");
    if (!subject) return Result<std::string>::failure(subject.status());
    Tactics* module = currentModule();
    if (!module)
        return failure<std::string>(DiagnosticCode::StaleHandle, "Tactics module is no longer loaded", "battle");
    auto created = module->newSide(session.value()->battle, subject.value());
    if (!created) return Result<std::string>::failure(created.status());
    std::move(created).takeValue();
    return Result<std::string>::success(subject.value().format(), Status::success(StatusCode::Applied));
}

Result<std::string> scriptAddUnit(ScriptTacticsBattle* proxy, const std::string& subjectText,
                                  const std::string& sideText, const std::string& definitionText, int x, int y,
                                  int layer, int actionPoints, int movePoints, int reactionPoints, int initiative) {
    auto session = bindingSession(proxy);
    if (!session) return Result<std::string>::failure(session.status());
    auto subject = bindingSubject(subjectText, "subject");
    auto definition = bindingLogicalId(definitionText, "definition");
    auto sideSubject = bindingSubject(sideText, "side");
    if (!subject) return Result<std::string>::failure(subject.status());
    if (!definition) return Result<std::string>::failure(definition.status());
    if (!sideSubject) return Result<std::string>::failure(sideSubject.status());
    auto* battle = dynamic_cast<Battle*>(ecs::try_get(session.value()->battle));
    if (!battle)
        return failure<std::string>(DiagnosticCode::StaleHandle, "tactics battle entity is stale", "battle");
    std::optional<ecs::EntityHandle> side;
    for (const auto& handle : battle->turn()->sides) {
        auto* sideValue = dynamic_cast<TacticalSide*>(ecs::try_get(handle));
        if (sideValue && sideValue->identity()->subject == sideSubject.value()) {
            side = handle;
            break;
        }
    }
    if (!side)
        return failure<std::string>(DiagnosticCode::NotFound, "tactics side is not part of this script battle",
                                    "side");
    Tactics* module = currentModule();
    if (!module)
        return failure<std::string>(DiagnosticCode::StaleHandle, "Tactics module is no longer loaded", "battle");
    auto created = module->newUnit(session.value()->battle, *side, subject.value(), definition.value(),
                                   {x, y, layer}, {actionPoints, movePoints, reactionPoints, initiative});
    if (!created) return Result<std::string>::failure(created.status());
    std::move(created).takeValue();
    return Result<std::string>::success(subject.value().format(), Status::success(StatusCode::Applied));
}

template <class ResultType, class Function>
ResultType withScriptBattle(ScriptTacticsBattle* proxy, Function&& function) {
    auto session = bindingSession(proxy);
    if (!session) return ResultType::failure(session.status());
    Tactics* module = currentModule();
    if (!module)
        return ResultType::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Tactics module is no longer loaded", "battle"));
    return std::invoke(std::forward<Function>(function), *module, *session.value());
}

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

bool controls(const GameplaySession& session, SubjectRef subject) {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), subject) !=
               session.controlledSubjects.end();
}

LogicalId gameplayId(std::string_view value) {
    return LogicalId::parse(value).value();
}

Value cellValue(Cell cell) {
    return Value(Value::Object{{"layer", Value(cell.layer)}, {"x", Value(cell.x)}, {"y", Value(cell.y)}});
}

Result<int> integerParameter(const Value& parameters, std::string_view name) {
    const auto* object = parameters.getIf<Value::Object>();
    const auto  found = object ? object->find(std::string(name)) : Value::Object::const_iterator{};
    if (!object || found == object->end() || !found->second.isInt64())
        return failure<int>(DiagnosticCode::InvalidArgument, "gameplay command parameter must be an integer",
                            "parameters." + std::string(name));
    const auto value = found->second.asInt();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument, "gameplay command parameter is out of range",
                            "parameters." + std::string(name));
    return Result<int>::success(static_cast<int>(value));
}

template <typename T>
std::size_t countLive(const std::vector<ecs::EntityHandle>& handles) {
    return static_cast<std::size_t>(std::count_if(handles.begin(), handles.end(), [](const auto& handle) {
        return dynamic_cast<T*>(ecs::try_get(handle)) != nullptr;
    }));
}

template <typename T>
void releaseLive(std::vector<ecs::EntityHandle>& handles) noexcept {
    for (const auto& handle : handles) {
        if (auto* entity = dynamic_cast<T*>(ecs::try_get(handle)); entity != nullptr) entity->release();
    }
    handles.clear();
}

}  // namespace

Module_IMPL(Tactics, new Tactics());

Tactics::Tactics() { cap::addListener<IGameplayControlProvider>(this); }

TacticsBattleSession::~TacticsBattleSession() noexcept {
    auto* value = dynamic_cast<Battle*>(ecs::try_get(battle));
    if (!value) return;
    auto units = value->turn()->units;
    auto sides = value->turn()->sides;
    releaseLive<TacticalUnit>(units);
    releaseLive<TacticalSide>(sides);
    value->release();
}

Tactics::~Tactics() {
    cap::removeListener<IGameplayControlProvider>(this);
    releaseLive<TacticalUnit>(units_);
    releaseLive<TacticalSide>(sides_);
    releaseLive<Battle>(battles_);
}

std::string_view Tactics::gameplayDomain() const noexcept { return "tactics"; }

Result<GameplayObservation> Tactics::observeGameplay(const GameplaySession& session,
                                                       SubjectRef instance) const {
    Battle* battle = resolveBattle(instance);
    if (!battle)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "tactics gameplay instance was not found",
                                            "instance");
    if (session.access == GameplayAccess::PlayerEquivalent) {
        const bool ownsParticipant = std::any_of(battle->turn()->units.begin(), battle->turn()->units.end(),
                                                 [&](const ecs::EntityHandle& handle) {
            auto* unit = dynamic_cast<TacticalUnit*>(ecs::try_get(handle));
            return unit && controls(session, unit->identity()->subject);
        });
        if (!ownsParticipant)
            return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                                "session controls no participant in this tactics battle",
                                                "session.controlledSubjects");
    }

    Value::Array units;
    for (const auto& handle : battle->turn()->units) {
        auto* unit = dynamic_cast<TacticalUnit*>(ecs::try_get(handle));
        if (!unit) continue;
        units.emplace_back(Value::Object{
            {"acted", Value(unit->turn()->acted)},
            {"actionPoints", Value(unit->turn()->actionPoints)},
            {"alive", Value(unit->turn()->alive)},
            {"cell", cellValue(unit->position()->cell)},
            {"controlled", Value(controls(session, unit->identity()->subject))},
            {"facing", Value(unit->position()->facing)},
            {"movePoints", Value(unit->turn()->movePoints)},
            {"subject", Value(unit->identity()->subject.format())},
        });
    }
    const auto active = battle->turn()->activeUnit
                            ? dynamic_cast<TacticalUnit*>(ecs::try_get(*battle->turn()->activeUnit))
                            : nullptr;
    GameplayObservation observation;
    observation.domain = gameplayId("gameplay:tactics");
    observation.instance = instance;
    observation.tick = battle->turn()->tick;
    observation.revision = battle->turn()->revision.value();
    observation.state = Value(Value::Object{
        {"activeUnit", Value(active ? active->identity()->subject.format() : std::string{})},
        {"phase", Value(std::string(phaseName(battle->turn()->phase)))},
        {"round", Value(static_cast<std::int64_t>(battle->turn()->round))},
        {"status", Value(std::string(statusName(battle->turn()->status)))},
        {"units", Value(std::move(units))},
    });
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> Tactics::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    Battle* battle = resolveBattle(instance);
    if (!battle)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::NotFound,
                                                               "tactics gameplay instance was not found", "instance");
    if (!controls(session, subject))
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                               "session does not control the requested subject",
                                                               "subject");
    auto* active = battle->turn()->activeUnit
                       ? dynamic_cast<TacticalUnit*>(ecs::try_get(*battle->turn()->activeUnit))
                       : nullptr;
    if (!active || active->identity()->subject != subject || !active->turn()->alive)
        return Result<std::vector<GameplayActionDescriptor>>::success({});

    Value integerSchema(Value::Object{{"type", Value("integer")}});
    Value moveSchema(Value::Object{{"layer", integerSchema}, {"x", integerSchema}, {"y", integerSchema}});
    Value faceSchema(Value::Object{{"facing", integerSchema}});
    std::vector<GameplayActionDescriptor> actions;
    if (battle->turn()->phase == BattlePhase::Acting) {
        actions.push_back({gameplayId("tactics:move"), std::move(moveSchema)});
        actions.push_back({gameplayId("tactics:face"), std::move(faceSchema)});
        actions.push_back({gameplayId("tactics:wait"), Value(Value::Object{})});
        actions.push_back({gameplayId("tactics:end-turn"), Value(Value::Object{})});
    }
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<GameplayCommandReceipt> Tactics::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                        const GameplayCommand& command) {
    Battle* battle = resolveBattle(instance);
    if (!battle)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "tactics gameplay instance was not found",
                                               "instance");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (!controls(session, command.subject))
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the command subject", "command.subject");
    if (command.observedTick != battle->turn()->tick ||
        command.expectedRevision != battle->turn()->revision.value())
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "gameplay command was based on a stale observation",
                                               "command.expectedRevision");

    Value details(Value::Object{});
    if (command.action == gameplayId("tactics:move")) {
        auto x = integerParameter(command.parameters, "x");
        auto y = integerParameter(command.parameters, "y");
        auto layer = integerParameter(command.parameters, "layer");
        if (!x) return Result<GameplayCommandReceipt>::failure(x.status());
        if (!y) return Result<GameplayCommandReceipt>::failure(y.status());
        if (!layer) return Result<GameplayCommandReceipt>::failure(layer.status());
        auto moved = BattleSystem::moveUnit(*battle, command.subject, {x.value(), y.value(), layer.value()},
                                            battle->turn()->tick);
        if (!moved) return Result<GameplayCommandReceipt>::failure(moved.status());
        const MoveReceipt receipt = std::move(moved).takeValue();
        details = Value(Value::Object{{"cost", Value(receipt.cost)},
                                      {"remainingMovePoints", Value(receipt.remainingMovePoints)}});
    } else if (command.action == gameplayId("tactics:face")) {
        auto facing = integerParameter(command.parameters, "facing");
        if (!facing) return Result<GameplayCommandReceipt>::failure(facing.status());
        auto faced = BattleSystem::faceUnit(*battle, command.subject, facing.value(), battle->turn()->tick);
        if (!faced) return Result<GameplayCommandReceipt>::failure(faced.status());
    } else if (command.action == gameplayId("tactics:wait")) {
        auto waited = BattleSystem::waitUnit(*battle, command.subject, battle->turn()->tick);
        if (!waited) return Result<GameplayCommandReceipt>::failure(waited.status());
    } else if (command.action == gameplayId("tactics:end-turn")) {
        auto ended = BattleSystem::endTurn(*battle, command.subject);
        if (!ended) return Result<GameplayCommandReceipt>::failure(ended.status());
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                               "unsupported tactics gameplay action", "command.action");
    }

    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = battle->commands()->values.empty()
                              ? std::string{}
                              : "tactics:" + std::to_string(battle->commands()->values.back().sequence);
    receipt.acceptedTick = battle->turn()->tick;
    receipt.resultingRevision = battle->turn()->revision.value();
    receipt.details = std::move(details);
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> Tactics::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                       const SimulationStep& step) {
    Battle* battle = resolveBattle(instance);
    if (!battle)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "tactics gameplay instance was not found",
                                            "instance");
    auto advanced = BattleSystem::advance(*battle, step);
    if (!advanced) return Result<GameplayObservation>::failure(advanced.status());
    std::move(advanced).takeValue();
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> Tactics::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                            std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    std::move(observed).takeValue();
    Battle* battle = resolveBattle(instance);
    std::vector<GameplayEvent> events;
    for (const auto& source : battle->events()->values) {
        if (source.sequence <= afterSequence) continue;
        GameplayEvent event;
        event.sequence = source.sequence;
        event.tick = source.tick;
        event.type = source.type;
        event.subject = source.subject;
        event.causationCommandId = source.causationCommand == 0
                                       ? std::string{}
                                       : "tactics:" + std::to_string(source.causationCommand);
        event.correlationId = source.correlationCommand == 0
                                  ? std::string{}
                                  : "tactics:" + std::to_string(source.correlationCommand);
        event.payload = Value(Value::Object{{"from", Value(std::string(phaseName(source.from)))},
                                            {"to", Value(std::string(phaseName(source.to)))}});
        events.push_back(std::move(event));
    }
    return Result<std::vector<GameplayEvent>>::success(std::move(events));
}

Result<ecs::EntityHandle> Tactics::newBattle(SubjectRef subject, std::uint64_t seed) {
    if (!subject.isValid())
        return failure<ecs::EntityHandle>(DiagnosticCode::InvalidArgument,
                                          "tactics battle requires a valid SubjectRef", "subject");
    Battle* battle             = Battle::create();
    battle->identity()->self    = ecs::handle_of(battle);
    battle->identity()->subject = subject;
    battle->identity()->seed    = seed;
    (void)battle->board();
    (void)battle->turn();
    (void)battle->events();
    (void)battle->reactions();
    (void)battle->commands();
    (void)battle->objectives();
    (void)battle->random();
    battles_.push_back(battle->identity()->self);
    return Result<ecs::EntityHandle>::success(battle->identity()->self, Status::success(StatusCode::Applied));
}

Result<ecs::EntityHandle> Tactics::newSide(ecs::EntityHandle battleHandle, SubjectRef subject) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<ecs::EntityHandle>(DiagnosticCode::StaleHandle,
                                          "tactics battle is stale or not owned by this facade", "battle");
    if (!subject.isValid())
        return failure<ecs::EntityHandle>(DiagnosticCode::InvalidArgument,
                                          "tactics side requires a valid SubjectRef", "subject");
    TacticalSide* side           = TacticalSide::create();
    side->identity()->self        = ecs::handle_of(side);
    side->identity()->subject     = subject;
    auto registered              = BattleSystem::addSide(*battle, side->identity()->self);
    if (!registered) {
        const Status status = registered.status();
        side->release();
        return Result<ecs::EntityHandle>::failure(status);
    }
    sides_.push_back(side->identity()->self);
    return Result<ecs::EntityHandle>::success(side->identity()->self, Status::success(StatusCode::Applied));
}

Result<ecs::EntityHandle> Tactics::newUnit(ecs::EntityHandle battleHandle, ecs::EntityHandle sideHandle,
                                           SubjectRef subject, LogicalId definition, Cell cellValue,
                                           TurnResourceSpec resources) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<ecs::EntityHandle>(DiagnosticCode::StaleHandle,
                                          "tactics battle is stale or not owned by this facade", "battle");
    if (!owns(sides_, sideHandle))
        return failure<ecs::EntityHandle>(DiagnosticCode::StaleHandle,
                                          "tactics side is stale or not owned by this facade", "side");
    if (!subject.isValid())
        return failure<ecs::EntityHandle>(DiagnosticCode::InvalidArgument,
                                          "tactics unit requires a valid SubjectRef", "subject");
    auto resourcesValid = resources.validate();
    if (!resourcesValid) return Result<ecs::EntityHandle>::failure(resourcesValid.status());
    TacticalUnit* unit               = TacticalUnit::create();
    unit->identity()->self            = ecs::handle_of(unit);
    unit->identity()->subject         = subject;
    unit->identity()->definition      = std::move(definition);
    (void)unit->membership();
    (void)unit->position();
    (void)unit->turn();
    unit->turn()->actionPoints   = resources.actionPoints;
    unit->turn()->movePoints     = resources.movePoints;
    unit->turn()->reactionPoints = resources.reactionPoints;
    unit->turn()->roundActionPoints   = resources.actionPoints;
    unit->turn()->roundMovePoints     = resources.movePoints;
    unit->turn()->roundReactionPoints = resources.reactionPoints;
    unit->turn()->initiative     = resources.initiative;
    auto registered = BattleSystem::addUnit(*battle, unit->identity()->self, sideHandle, cellValue);
    if (!registered) {
        const Status status = registered.status();
        unit->release();
        return Result<ecs::EntityHandle>::failure(status);
    }
    units_.push_back(unit->identity()->self);
    return Result<ecs::EntityHandle>::success(unit->identity()->self, Status::success(StatusCode::Applied));
}

Result<void> Tactics::addCell(ecs::EntityHandle battleHandle, Cell cellValue, CellState state) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    if (battle->turn()->status != BattleStatus::Setup)
        return failure(DiagnosticCode::PreconditionViolation, "tactics cells can only be added during setup",
                       "battle.status");
    const auto nextRevision = battle->turn()->revision.incremented();
    if (!nextRevision)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle revision is exhausted",
                       "battle.revision");
    auto added = battle->board()->value.addCell(cellValue, std::move(state));
    if (!added) return added;
    battle->turn()->revision = *nextRevision;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Tactics::setTopology(ecs::EntityHandle battleHandle, BoardTopology topology) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    if (battle->turn()->status != BattleStatus::Setup)
        return failure(DiagnosticCode::PreconditionViolation, "tactics topology can only change during setup",
                       "battle.status");
    if (battle->board()->value.topology() == topology)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    const auto revision = battle->turn()->revision.incremented();
    if (!revision)
        return failure(DiagnosticCode::PreconditionViolation, "tactics battle revision is exhausted",
                       "battle.revision");
    battle->board()->value.setTopology(topology);
    battle->turn()->revision = *revision;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> Tactics::start(ecs::EntityHandle battleHandle, TurnPolicyKind policy) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::start(*battle, policy);
}

Result<BattlePhase> Tactics::advance(ecs::EntityHandle battleHandle, const SimulationStep& step) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<BattlePhase>(DiagnosticCode::StaleHandle,
                                    "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::advance(*battle, step);
}

Result<void> Tactics::endTurn(ecs::EntityHandle battleHandle, SubjectRef actor) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::endTurn(*battle, actor);
}

Result<MoveReceipt> Tactics::moveUnit(ecs::EntityHandle battleHandle, SubjectRef actor, Cell destination) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<MoveReceipt>(DiagnosticCode::StaleHandle,
                                    "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::moveUnit(*battle, actor, destination);
}

Result<void> Tactics::faceUnit(ecs::EntityHandle battleHandle, SubjectRef actor, int facing) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::faceUnit(*battle, actor, facing);
}

Result<void> Tactics::waitUnit(ecs::EntityHandle battleHandle, SubjectRef actor) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::waitUnit(*battle, actor);
}

Result<void> Tactics::finish(ecs::EntityHandle battleHandle) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::finish(*battle);
}

Result<std::size_t> Tactics::openReaction(ecs::EntityHandle battleHandle, std::uint64_t triggerSequence,
                                          std::vector<ReactionCandidate> candidates) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                    "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::openReaction(*battle, triggerSequence, std::move(candidates));
}

Result<ReactionReceipt> Tactics::acceptReaction(ecs::EntityHandle battleHandle, SubjectRef reactor,
                                                 const LogicalId& action) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<ReactionReceipt>(DiagnosticCode::StaleHandle,
                                        "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::acceptReaction(*battle, reactor, action);
}

Result<void> Tactics::declineReaction(ecs::EntityHandle battleHandle) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::declineReaction(*battle);
}

Result<void> Tactics::addObjective(ecs::EntityHandle battleHandle, ObjectiveSpec objective) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::addObjective(*battle, std::move(objective));
}

Result<void> Tactics::defeatUnit(ecs::EntityHandle battleHandle, SubjectRef unit) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::defeatUnit(*battle, unit);
}

Result<std::uint64_t> Tactics::roll(ecs::EntityHandle battleHandle, const LogicalId& stream) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure<std::uint64_t>(DiagnosticCode::StaleHandle,
                                      "tactics battle is stale or not owned by this facade", "battle");
    return BattleSystem::roll(*battle, stream);
}

Result<BattleStatus> Tactics::status(ecs::EntityHandle battleHandle) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<BattleStatus>(DiagnosticCode::StaleHandle,
                                     "tactics battle is stale or not owned by this facade", "battle");
    return Result<BattleStatus>::success(battle->turn()->status);
}

Result<BattlePhase> Tactics::phase(ecs::EntityHandle battleHandle) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<BattlePhase>(DiagnosticCode::StaleHandle,
                                    "tactics battle is stale or not owned by this facade", "battle");
    return Result<BattlePhase>::success(battle->turn()->phase);
}

Result<SubjectRef> Tactics::activeUnit(ecs::EntityHandle battleHandle) {
    Battle* battle = resolveBattle(battleHandle);
    if (battle == nullptr)
        return failure<SubjectRef>(DiagnosticCode::StaleHandle,
                                   "tactics battle is stale or not owned by this facade", "battle");
    if (!battle->turn()->activeUnit)
        return failure<SubjectRef>(DiagnosticCode::NotFound, "tactics battle has no active unit",
                                   "battle.activeUnit");
    auto* unit = dynamic_cast<TacticalUnit*>(ecs::try_get(*battle->turn()->activeUnit));
    if (unit == nullptr)
        return failure<SubjectRef>(DiagnosticCode::StaleHandle, "tactics active unit is stale",
                                   "battle.activeUnit");
    return Result<SubjectRef>::success(unit->identity()->subject);
}

Result<SnapshotEnvelope> Tactics::snapshot(ecs::EntityHandle battleHandle,
                                           const SnapshotHashProvider& hashProvider) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure<SnapshotEnvelope>(DiagnosticCode::StaleHandle,
                                         "tactics battle is stale or not owned by this facade", "battle");
    return TacticsPersistence::snapshot(*battle, hashProvider);
}

Result<void> Tactics::restore(ecs::EntityHandle battleHandle, const SnapshotEnvelope& snapshotValue,
                              const SnapshotHashProvider& hashProvider) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return TacticsPersistence::restore(*battle, snapshotValue, hashProvider);
}

Result<std::vector<BattleCommand>> Tactics::commandsFrom(ecs::EntityHandle battleHandle, Revision revision) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure<std::vector<BattleCommand>>(DiagnosticCode::StaleHandle,
                                                   "tactics battle is stale or not owned by this facade", "battle");
    return Result<std::vector<BattleCommand>>::success(BattleReplay::commandsFrom(*battle, revision));
}

Result<void> Tactics::replay(ecs::EntityHandle battleHandle, std::span<const BattleCommand> commands) {
    Battle* battle = resolveBattle(battleHandle);
    if (!battle)
        return failure(DiagnosticCode::StaleHandle, "tactics battle is stale or not owned by this facade", "battle");
    return BattleReplay::replay(*battle, commands);
}

std::size_t Tactics::battleCount() const noexcept { return countLive<Battle>(battles_); }
std::size_t Tactics::unitCount() const noexcept { return countLive<TacticalUnit>(units_); }
std::size_t Tactics::sideCount() const noexcept { return countLive<TacticalSide>(sides_); }

Result<TacticsBattleSessionRef> Tactics::newSession(SubjectRef subject, std::uint64_t seed) {
    Tactics* module = Tactics::create();
    auto battle = module->newBattle(subject, seed);
    if (!battle) return Result<TacticsBattleSessionRef>::failure(battle.status());
    auto session = std::make_unique<TacticsBattleSession>();
    session->battle = std::move(battle).takeValue();
    auto inserted = module->sessions_.emplace(std::move(session));
    if (!inserted) return Result<TacticsBattleSessionRef>::failure(inserted.status());
    return inserted;
}

script::Borrowed<TacticsBattleSession> Tactics::resolve(TacticsBattleSessionRef reference) noexcept {
    Tactics* module = ModuleManager::getInstance<Tactics>("Tactics");
    return module ? module->sessions_.resolve(reference) : script::Borrowed<TacticsBattleSession>();
}

Result<void> Tactics::release(TacticsBattleSessionRef reference) {
    Tactics* module = ModuleManager::getInstance<Tactics>("Tactics");
    if (!module)
        return failure(DiagnosticCode::StaleHandle, "Tactics module is no longer loaded", "battle");
    return module->sessions_.erase(reference);
}

bool Tactics::isStale(TacticsBattleSessionRef reference) noexcept {
    if (!reference.isValid()) return false;
    Tactics* module = ModuleManager::getInstance<Tactics>("Tactics");
    return !module || module->sessions_.isStale(reference);
}

Battle* Tactics::resolveBattle(ecs::EntityHandle handle) const noexcept {
    return owns(battles_, handle) ? dynamic_cast<Battle*>(ecs::try_get(handle)) : nullptr;
}

Battle* Tactics::resolveBattle(SubjectRef subject) const noexcept {
    const auto found = std::find_if(battles_.begin(), battles_.end(), [&](const ecs::EntityHandle& handle) {
        auto* battle = dynamic_cast<Battle*>(ecs::try_get(handle));
        return battle && battle->identity()->subject == subject;
    });
    return found == battles_.end() ? nullptr : dynamic_cast<Battle*>(ecs::try_get(*found));
}

bool Tactics::owns(const std::vector<ecs::EntityHandle>& handles, const ecs::EntityHandle& handle) const noexcept {
    return std::any_of(handles.begin(), handles.end(), [&](const auto& value) { return sameHandle(value, handle); }) &&
           ecs::try_get(handle) != nullptr;
}

void Tactics::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto battle = table.addClass<ScriptTacticsBattle>(
        "TacticsBattle", std::function<ScriptTacticsBattle*()>([]() -> ScriptTacticsBattle* { return nullptr; }),
        false);
    battle.addFunc("ownership", [](ScriptTacticsBattle*) { return std::string("owned"); });
    battle.addFunc("handle", [](ScriptTacticsBattle* value) {
        return value ? static_cast<std::int64_t>(value->reference.packed()) : std::int64_t{0};
    });
    battle.addFunc("isStale", [](ScriptTacticsBattle* value) {
        return !value || Tactics::isStale(value->reference);
    });
    battle.addFunc("release", [vm](ScriptTacticsBattle* value) {
        if (!value)
            return script::projectResult(
                vm, failure(DiagnosticCode::InvalidArgument, "tactics battle proxy must not be null", "battle"));
        return script::projectResult(vm, Tactics::release(value->reference));
    });
    battle.addFunc("addCell", [vm](ScriptTacticsBattle* value, int x, int y, int layer, int moveCost) {
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                CellState state;
                state.moveCost = moveCost;
                return module.addCell(session.battle, {x, y, layer}, std::move(state));
            }));
    });
    battle.addFunc("setTopology", [vm](ScriptTacticsBattle* value, const std::string& topology) {
        BoardTopology parsed;
        if (topology == "square4")
            parsed = BoardTopology::Square4;
        else if (topology == "square8")
            parsed = BoardTopology::Square8;
        else if (topology == "hex")
            parsed = BoardTopology::HexAxial;
        else
            return script::projectResult(
                vm, failure(DiagnosticCode::InvalidArgument, "unknown tactics board topology", "topology"));
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.setTopology(session.battle, parsed);
            }));
    });
    battle.addFunc("addSide", [vm](ScriptTacticsBattle* value, const std::string& subject) {
        return script::projectResult(vm, scriptAddSide(value, subject),
                                     [](std::string result) { return Value(std::move(result)); });
    });
    battle.addFunc("addUnit", [vm](ScriptTacticsBattle* value, const std::string& subject,
                                    const std::string& side, const std::string& definition, int x, int y, int layer,
                                    int actionPoints, int movePoints, int reactionPoints, int initiative) {
        return script::projectResult(
            vm,
            scriptAddUnit(value, subject, side, definition, x, y, layer, actionPoints, movePoints, reactionPoints,
                          initiative),
            [](std::string result) { return Value(std::move(result)); });
    });
    battle.addFunc("addEliminateObjective", [vm](ScriptTacticsBattle* value, const std::string& idText,
                                                  const std::string& beneficiaryText,
                                                  const std::string& targetText, bool endsBattle) {
        auto id = bindingLogicalId(idText, "id");
        auto beneficiary = bindingSubject(beneficiaryText, "beneficiarySide");
        auto target = bindingSubject(targetText, "targetSide");
        if (!id) return script::projectResult(vm, Result<void>::failure(id.status()));
        if (!beneficiary) return script::projectResult(vm, Result<void>::failure(beneficiary.status()));
        if (!target) return script::projectResult(vm, Result<void>::failure(target.status()));
        ObjectiveSpec objective;
        objective.id = id.value();
        objective.kind = ObjectiveKind::EliminateSide;
        objective.beneficiarySide = beneficiary.value();
        objective.targetSide = target.value();
        objective.endsBattle = endsBattle;
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.addObjective(session.battle, objective);
            }));
    });
    battle.addFunc("addSurviveObjective", [vm](ScriptTacticsBattle* value, const std::string& idText,
                                                const std::string& beneficiaryText, std::int64_t requiredRound,
                                                bool endsBattle) {
        auto id = bindingLogicalId(idText, "id");
        auto beneficiary = bindingSubject(beneficiaryText, "beneficiarySide");
        if (!id) return script::projectResult(vm, Result<void>::failure(id.status()));
        if (!beneficiary) return script::projectResult(vm, Result<void>::failure(beneficiary.status()));
        if (requiredRound <= 0)
            return script::projectResult(
                vm, failure(DiagnosticCode::InvalidArgument, "required round must be positive", "requiredRound"));
        ObjectiveSpec objective;
        objective.id = id.value();
        objective.kind = ObjectiveKind::SurviveRounds;
        objective.beneficiarySide = beneficiary.value();
        objective.requiredRound = static_cast<std::uint64_t>(requiredRound);
        objective.endsBattle = endsBattle;
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.addObjective(session.battle, objective);
            }));
    });
    battle.addFunc("addOccupyObjectiveJson", [vm](ScriptTacticsBattle* value, const std::string& idText,
                                                   const std::string& beneficiaryText,
                                                   const std::string& cellsJson, bool endsBattle) {
        auto id = bindingLogicalId(idText, "id");
        auto beneficiary = bindingSubject(beneficiaryText, "beneficiarySide");
        auto cells = bindingCellsJson(cellsJson);
        if (!id) return script::projectResult(vm, Result<void>::failure(id.status()));
        if (!beneficiary) return script::projectResult(vm, Result<void>::failure(beneficiary.status()));
        if (!cells) return script::projectResult(vm, Result<void>::failure(cells.status()));
        ObjectiveSpec objective;
        objective.id = id.value();
        objective.kind = ObjectiveKind::OccupyCells;
        objective.beneficiarySide = beneficiary.value();
        objective.requiredCells = std::move(cells).takeValue();
        objective.endsBattle = endsBattle;
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.addObjective(session.battle, objective);
            }));
    });
    battle.addFunc("start", [vm](ScriptTacticsBattle* value, const std::string& policy) {
        TurnPolicyKind parsed;
        if (policy == "initiative")
            parsed = TurnPolicyKind::Initiative;
        else if (policy == "side_alternating")
            parsed = TurnPolicyKind::SideAlternating;
        else
            return script::projectResult(
                vm, failure(DiagnosticCode::InvalidArgument, "unknown tactics turn policy", "policy"));
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.start(session.battle, parsed);
            }));
    });
    battle.addFunc("advance", [vm](ScriptTacticsBattle* value, std::int64_t tick, std::int64_t deltaNanoseconds) {
        if (tick < 0)
            return script::projectResult(
                vm, failure<BattlePhase>(DiagnosticCode::InvalidArgument, "simulation tick must be non-negative",
                                         "tick"),
                [](BattlePhase phase) { return Value(std::string(phaseName(phase))); });
        return script::projectResult(
            vm,
            withScriptBattle<Result<BattlePhase>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.advance(session.battle,
                                      {SimulationTick(static_cast<std::uint64_t>(tick)),
                                       Duration::fromNanoseconds(deltaNanoseconds)});
            }),
            [](BattlePhase phase) { return Value(std::string(phaseName(phase))); });
    });
    battle.addFunc("move", [vm](ScriptTacticsBattle* value, const std::string& actor, int x, int y, int layer) {
        auto subject = bindingSubject(actor, "actor");
        if (!subject)
            return script::projectResult(vm, Result<MoveReceipt>::failure(subject.status()), [](MoveReceipt receipt) {
                return Value(receipt.remainingMovePoints);
            });
        return script::projectResult(
            vm,
            withScriptBattle<Result<MoveReceipt>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.moveUnit(session.battle, subject.value(), {x, y, layer});
            }),
            [](MoveReceipt receipt) {
                Value::Array path;
                for (const Cell cell : receipt.path) path.push_back(bindingCellValue(cell));
                return Value(Value::Object{{"cost", Value(receipt.cost)},
                                           {"path", Value(std::move(path))},
                                           {"remainingMovePoints", Value(receipt.remainingMovePoints)}});
            });
    });
    battle.addFunc("face", [vm](ScriptTacticsBattle* value, const std::string& actor, int facing) {
        auto subject = bindingSubject(actor, "actor");
        if (!subject) return script::projectResult(vm, Result<void>::failure(subject.status()));
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.faceUnit(session.battle, subject.value(), facing);
            }));
    });
    battle.addFunc("wait", [vm](ScriptTacticsBattle* value, const std::string& actor) {
        auto subject = bindingSubject(actor, "actor");
        if (!subject) return script::projectResult(vm, Result<void>::failure(subject.status()));
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.waitUnit(session.battle, subject.value());
            }));
    });
    battle.addFunc("defeatUnit", [vm](ScriptTacticsBattle* value, const std::string& unit) {
        auto subject = bindingSubject(unit, "unit");
        if (!subject) return script::projectResult(vm, Result<void>::failure(subject.status()));
        return script::projectResult(
            vm, withScriptBattle<Result<void>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.defeatUnit(session.battle, subject.value());
            }));
    });
    battle.addFunc("roll", [vm](ScriptTacticsBattle* value, const std::string& streamText) {
        auto stream = bindingLogicalId(streamText, "stream");
        if (!stream)
            return script::projectResult(vm, Result<std::uint64_t>::failure(stream.status()),
                                         [](std::uint64_t value) { return Value(std::to_string(value)); });
        return script::projectResult(
            vm,
            withScriptBattle<Result<std::uint64_t>>(value, [&](Tactics& module, TacticsBattleSession& session) {
                return module.roll(session.battle, stream.value());
            }),
            [](std::uint64_t value) { return Value(std::to_string(value)); });
    });
    battle.addFunc("status", [vm](ScriptTacticsBattle* value) {
        return script::projectResult(
            vm, withScriptBattle<Result<BattleStatus>>(value, [](Tactics& module, TacticsBattleSession& session) {
                return module.status(session.battle);
            }),
            [](BattleStatus status) { return Value(std::string(statusName(status))); });
    });
    battle.addFunc("phase", [vm](ScriptTacticsBattle* value) {
        return script::projectResult(
            vm, withScriptBattle<Result<BattlePhase>>(value, [](Tactics& module, TacticsBattleSession& session) {
                return module.phase(session.battle);
            }),
            [](BattlePhase phase) { return Value(std::string(phaseName(phase))); });
    });
    battle.addFunc("activeUnit", [vm](ScriptTacticsBattle* value) {
        return script::projectResult(
            vm, withScriptBattle<Result<SubjectRef>>(value, [](Tactics& module, TacticsBattleSession& session) {
                return module.activeUnit(session.battle);
            }),
            [](SubjectRef subject) { return Value(subject.format()); });
    });
    battle.addFunc("unitCell", [vm](ScriptTacticsBattle* value, const std::string& unitText) {
        auto subject = bindingSubject(unitText, "unit");
        if (!subject)
            return script::projectResult(vm, Result<Cell>::failure(subject.status()), bindingCellValue);
        auto session = bindingSession(value);
        if (!session)
            return script::projectResult(vm, Result<Cell>::failure(session.status()), bindingCellValue);
        auto* battleValue = dynamic_cast<Battle*>(ecs::try_get(session.value()->battle));
        if (!battleValue)
            return script::projectResult(
                vm, failure<Cell>(DiagnosticCode::StaleHandle, "tactics battle entity is stale", "battle"),
                bindingCellValue);
        const auto cell = battleValue->board()->value.position(subject.value());
        if (!cell)
            return script::projectResult(
                vm, failure<Cell>(DiagnosticCode::NotFound, "tactics unit is not placed", "unit"),
                bindingCellValue);
        return script::projectResult(vm, Result<Cell>::success(*cell), bindingCellValue);
    });
    battle.addFunc("revision", [](ScriptTacticsBattle* value) -> std::int64_t {
        auto session = value ? Tactics::resolve(value->reference) : script::Borrowed<TacticsBattleSession>();
        auto* battleValue = session.isBound() ? dynamic_cast<Battle*>(ecs::try_get(session->battle)) : nullptr;
        if (!battleValue || battleValue->turn()->revision.value() > static_cast<std::uint64_t>(INT64_MAX)) return -1;
        return static_cast<std::int64_t>(battleValue->turn()->revision.value());
    });
    battle.addFunc("eventCount", [](ScriptTacticsBattle* value) -> std::int64_t {
        auto session = value ? Tactics::resolve(value->reference) : script::Borrowed<TacticsBattleSession>();
        auto* battleValue = session.isBound() ? dynamic_cast<Battle*>(ecs::try_get(session->battle)) : nullptr;
        return battleValue ? static_cast<std::int64_t>(battleValue->events()->values.size()) : 0;
    });
    battle.addFunc("eventAt", [vm](ScriptTacticsBattle* value, int index) {
        auto session = bindingSession(value);
        if (!session)
            return script::projectResult(vm, Result<BattleEvent>::failure(session.status()), eventProjection);
        auto* battleValue = dynamic_cast<Battle*>(ecs::try_get(session.value()->battle));
        if (!battleValue || index < 0 || static_cast<std::size_t>(index) >= battleValue->events()->values.size())
            return script::projectResult(
                vm, failure<BattleEvent>(DiagnosticCode::NotFound, "tactics event index is out of range", "index"),
                eventProjection);
        return script::projectResult(
            vm, Result<BattleEvent>::success(battleValue->events()->values[static_cast<std::size_t>(index)]),
            eventProjection);
    });

    auto cls = table.addClass(name, Tactics::create, false);
    expose(cls);
}

void Tactics::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Tactics::getName);
    cls.addFunc("battleCount", [](Tactics* self) { return self->battleCount(); });
    cls.addFunc("unitCount", [](Tactics* self) { return self->unitCount(); });
    cls.addFunc("sideCount", [](Tactics* self) { return self->sideCount(); });
    cls.addFunc("newBattle", [vm = cls.getHandle()](Tactics*, const std::string& subject, std::int64_t seed) {
        auto parsed = bindingSubject(subject, "subject");
        if (!parsed)
            return script::projectStatusResult(vm, parsed.status(), false, false);
        if (seed < 0)
            return script::projectResult(
                vm, failure<TacticsBattleSessionRef>(DiagnosticCode::InvalidArgument,
                                                      "battle seed must be non-negative", "seed"),
                [](TacticsBattleSessionRef) { return Value(); });
        auto reference = Tactics::newSession(parsed.value(), static_cast<std::uint64_t>(seed));
        if (!reference) return script::projectStatusResult(vm, reference.status(), false, false);
        const auto ref = std::move(reference).takeValue();
        auto object = script::makeOwnedSquirrelInstance<ScriptTacticsBattle>(
            vm, std::make_unique<ScriptTacticsBattle>(ref));
        if (!object) {
            const Status status = object.status();
            object.ignore("failed to create owned tactics battle proxy");
            Tactics::release(ref).ignore("rollback failed owned tactics battle allocation");
            return script::projectStatusResult(vm, status, false, false);
        }
        ssq::Object owned = std::move(object).takeValue();
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
        result.set("handle", static_cast<std::int64_t>(ref.packed()));
        return result;
    });
}

}  // namespace eve::tactics
