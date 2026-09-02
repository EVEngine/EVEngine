#include "rts/RTS.h"
#include "rts/RTSAttributes.h"

#include "action/Action.h"
#include "common/Capability.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <utility>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool validSubject(SubjectRef subject) { return subject.isValid(); }

bool controls(const GameplaySession& session, SubjectRef subject) {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), subject) !=
               session.controlledSubjects.end();
}

LogicalId gameplayId(std::string_view value) { return LogicalId::parse(value).value(); }

Result<double> numericParameter(const Value& parameters, std::string_view name) {
    const auto* object = parameters.getIf<Value::Object>();
    if (!object)
        return failure<double>(DiagnosticCode::InvalidArgument, "RTS gameplay parameters must be an object",
                               "parameters");
    const auto found = object->find(std::string(name));
    if (found == object->end() || !found->second.isNumeric())
        return failure<double>(DiagnosticCode::InvalidArgument, "RTS gameplay parameter must be numeric",
                               "parameters." + std::string(name));
    return Result<double>::success(found->second.isInt64() ? static_cast<double>(found->second.asInt())
                                                           : found->second.asDouble());
}

template <typename T>
void destroyHandles(std::vector<ecs::EntityHandle>& handles) {
    for (const auto& handle : handles) {
        if (auto* entity = ecs::try_get(handle)) {
            if (auto* typed = dynamic_cast<T*>(entity)) typed->release();
        }
    }
    handles.clear();
}

template <typename T>
std::size_t countLive(const std::vector<ecs::EntityHandle>& handles) {
    return static_cast<std::size_t>(std::count_if(handles.begin(), handles.end(), [](const ecs::EntityHandle& handle) {
        return dynamic_cast<T*>(ecs::try_get(handle)) != nullptr;
    }));
}

template <typename T>
bool owns(const std::vector<ecs::EntityHandle>& handles, const T& entity) {
    const auto live = ecs::handle_of(const_cast<T*>(&entity));
    return std::any_of(handles.begin(), handles.end(), [&live](const auto& handle) {
        return handle.table == live.table && handle.type == live.type && handle.id == live.id &&
               handle.generation == live.generation;
    });
}

}  // namespace

Module_IMPL(RTS, new RTS());

struct RTS::GameplayRuntime {
    action::ActionRuntime action;
    ActionAdapter         adapter{action};
};

RTS::RTS() : gameplayRuntime_(std::make_unique<GameplayRuntime>()) {
    cap::addListener<IGameplayControlProvider>(this);
}

RTS::~RTS() {
    cap::removeListener<IGameplayControlProvider>(this);
    destroyHandles<Unit>(units_);
    destroyHandles<Building>(buildings_);
    destroyHandles<Player>(players_);
    destroyHandles<Faction>(factions_);
}

std::string_view RTS::gameplayDomain() const noexcept { return "rts"; }

Result<GameplayObservation> RTS::observeGameplay(const GameplaySession& session, SubjectRef instance) const {
    Player* player = resolvePlayer(instance);
    if (!player)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "RTS gameplay player was not found",
                                            "instance");
    if (!controls(session, instance))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control this RTS player", "instance");
    Value::Array units;
    for (const auto& handle : player->selection()->units) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (!unit) continue;
        auto current = unit->orders()->values.current();
        std::string orderKind;
        if (current) {
            orderKind = orderKindName(current.value().kind);
        } else if (current.code() == StatusCode::NotFound) {
            current.ignore("idle RTS unit has no active order");
        } else {
            return Result<GameplayObservation>::failure(current.status());
        }
        units.emplace_back(Value::Object{{"activeOrder", Value(std::move(orderKind))},
                                         {"arrived", Value(unit->motion()->arrived)},
                                         {"subject", Value(unit->identity()->subject.format())},
                                         {"x", Value(unit->motion()->x)},
                                         {"y", Value(unit->motion()->y)}});
    }
    GameplayObservation observation;
    observation.domain = gameplayId("gameplay:rts");
    observation.instance = instance;
    observation.tick = player->selection()->tick;
    observation.revision = player->selection()->revision;
    observation.state = Value(Value::Object{{"selectedUnits", Value(std::move(units))}});
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> RTS::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    Player* player = resolvePlayer(instance);
    Unit* unit = resolveUnit(subject);
    if (!player || !unit)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::NotFound,
                                                               "RTS gameplay player or unit was not found",
                                                               "subject");
    if (!controls(session, instance))
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                               "session does not control this RTS player",
                                                               "instance");
    const auto unitHandle = ecs::handle_of(unit);
    const bool selected = std::any_of(player->selection()->units.begin(), player->selection()->units.end(),
                                      [&](const auto& handle) {
        return handle.table == unitHandle.table && handle.type == unitHandle.type && handle.id == unitHandle.id &&
               handle.generation == unitHandle.generation;
    });
    if (!selected)
        return failure<std::vector<GameplayActionDescriptor>>(DiagnosticCode::PreconditionViolation,
                                                               "RTS unit is not in the player's selection",
                                                               "subject");
    Value number(Value::Object{{"type", Value("number")}});
    Value schema(Value::Object{{"x", number}, {"y", number}});
    std::vector<GameplayActionDescriptor> actions;
    actions.push_back({gameplayId("rts:move"), schema});
    actions.push_back({gameplayId("rts:attack"), std::move(schema)});
    return Result<std::vector<GameplayActionDescriptor>>::success(std::move(actions));
}

Result<GameplayCommandReceipt> RTS::submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                    const GameplayCommand& command) {
    Player* player = resolvePlayer(instance);
    if (!player)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound, "RTS gameplay player was not found",
                                               "instance");
    if (!controls(session, instance))
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control this RTS player", "instance");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument, "command id must not be empty",
                                               "command.id");
    if (command.observedTick != player->selection()->tick ||
        command.expectedRevision != player->selection()->revision)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "RTS command was based on a stale observation",
                                               "command.expectedRevision");
    auto x = numericParameter(command.parameters, "x");
    auto y = numericParameter(command.parameters, "y");
    if (!x) return Result<GameplayCommandReceipt>::failure(x.status());
    if (!y) return Result<GameplayCommandReceipt>::failure(y.status());

    CommandSpec spec;
    if (command.action == gameplayId("rts:move")) {
        spec.kind = OrderKind::Move;
        spec.definitionId = "rts:move";
    } else if (command.action == gameplayId("rts:attack")) {
        spec.kind = OrderKind::Attack;
        spec.definitionId = "rts:attack";
    } else {
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported, "unsupported RTS gameplay action",
                                               "command.action");
    }
    spec.target = {static_cast<float>(x.value()), static_cast<float>(y.value())};
    FormationSpec formation;
    auto accepted = fanOut(*player->selection(), spec, formation);
    if (!accepted) return Result<GameplayCommandReceipt>::failure(accepted.status());
    auto fanOutReceipt = std::move(accepted).takeValue();

    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = fanOutReceipt.orderIds.empty() ? std::string{} : fanOutReceipt.orderIds.front();
    receipt.acceptedTick = player->selection()->tick;
    receipt.resultingRevision = player->selection()->revision;
    Value::Array orderIds;
    for (auto& id : fanOutReceipt.orderIds) orderIds.emplace_back(std::move(id));
    receipt.details = Value(Value::Object{{"accepted", Value(static_cast<std::int64_t>(fanOutReceipt.accepted))},
                                          {"orderIds", Value(std::move(orderIds))}});
    GameplayEvent event;
    event.sequence = nextGameplayEventSequence_++;
    event.tick = player->selection()->tick;
    event.type = "rts.command.accepted";
    event.subject = command.subject;
    event.causationCommandId = command.id;
    event.correlationId = command.id;
    event.payload = Value(Value::Object{{"action", Value(command.action.format())},
                                        {"instance", Value(instance.format())},
                                        {"resultingRevision",
                                         Value(static_cast<std::int64_t>(player->selection()->revision))}});
    gameplayEvents_.push_back(std::move(event));
    return Result<GameplayCommandReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<GameplayObservation> RTS::advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                  const SimulationStep& simulationStep) {
    Player* player = resolvePlayer(instance);
    if (!player)
        return failure<GameplayObservation>(DiagnosticCode::NotFound, "RTS gameplay player was not found",
                                            "instance");
    if (!controls(session, instance))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control this RTS player", "instance");
    if (simulationStep.tick <= player->selection()->tick)
        return failure<GameplayObservation>(DiagnosticCode::Conflict, "RTS simulation tick must increase",
                                            "step.tick");
    auto advanced = step(simulationStep, gameplayRuntime_->adapter);
    if (!advanced) return Result<GameplayObservation>::failure(advanced.status());
    std::move(advanced).takeValue();
    player->selection()->tick = simulationStep.tick;
    ++player->selection()->revision;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> RTS::gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                        std::uint64_t afterSequence) const {
    auto observation = observeGameplay(session, instance);
    if (!observation) return Result<std::vector<GameplayEvent>>::failure(observation.status());
    std::move(observation).takeValue();
    std::vector<GameplayEvent> result;
    for (const auto& event : gameplayEvents_) {
        const auto* eventInstance = event.payload.find("instance");
        if (event.sequence > afterSequence && eventInstance && eventInstance->isString() &&
            eventInstance->asString() == instance.format())
            result.push_back(event);
    }
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(
        std::move(result), Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

Result<Unit*> RTS::newUnit(SubjectRef subject, LogicalId definition) {
    if (!validSubject(subject))
        return failure<Unit*>(DiagnosticCode::InvalidArgument, "RTS Unit requires a valid SubjectRef", "subject");
    Unit* unit = Unit::createUnit(subject, std::move(definition));
    if (unit == nullptr) return failure<Unit*>(DiagnosticCode::Failed, "ECS failed to create an RTS Unit", "unit");
    auto attributes = RTSUnitAttributeAdapter::ensure(*unit);
    if (!attributes) {
        const auto status = attributes.status();
        unit->release();
        return Result<Unit*>::failure(status);
    }
    auto effects = unit->effects()->values.bindSubject(subject);
    if (!effects) {
        const auto status = effects.status();
        unit->release();
        return Result<Unit*>::failure(status);
    }
    units_.push_back(ecs::handle_of(unit));
    return Result<Unit*>::success(unit, Status::success(StatusCode::Applied));
}

Result<Building*> RTS::newBuilding(SubjectRef subject, LogicalId definition) {
    if (!validSubject(subject))
        return failure<Building*>(DiagnosticCode::InvalidArgument, "RTS Building requires a valid SubjectRef",
                                  "subject");
    Building* building = Building::createBuilding(subject, std::move(definition));
    if (building == nullptr)
        return failure<Building*>(DiagnosticCode::Failed, "ECS failed to create an RTS Building", "building");
    auto effects = building->effects()->values.bindSubject(subject);
    if (!effects) {
        const auto status = effects.status();
        building->release();
        return Result<Building*>::failure(status);
    }
    buildings_.push_back(ecs::handle_of(building));
    return Result<Building*>::success(building, Status::success(StatusCode::Applied));
}

Result<Player*> RTS::newPlayer(SubjectRef subject) {
    if (!validSubject(subject))
        return failure<Player*>(DiagnosticCode::InvalidArgument, "RTS Player requires a valid SubjectRef", "subject");
    Player* player = Player::createPlayer(subject);
    if (player == nullptr)
        return failure<Player*>(DiagnosticCode::Failed, "ECS failed to create an RTS Player", "player");
    players_.push_back(ecs::handle_of(player));
    return Result<Player*>::success(player, Status::success(StatusCode::Applied));
}

Result<Faction*> RTS::newFaction(SubjectRef subject) {
    if (!validSubject(subject))
        return failure<Faction*>(DiagnosticCode::InvalidArgument, "RTS Faction requires a valid SubjectRef", "subject");
    Faction* faction = Faction::createFaction(subject);
    if (faction == nullptr)
        return failure<Faction*>(DiagnosticCode::Failed, "ECS failed to create an RTS Faction", "faction");
    factions_.push_back(ecs::handle_of(faction));
    return Result<Faction*>::success(faction, Status::success(StatusCode::Applied));
}

Result<FanOutReceipt> RTS::fanOut(Player::Selection& selection, const CommandSpec& command,
                                  const FormationSpec& formation) const {
    auto result = CommandFanOutSystem::fanOut(selection.units, command, formation);
    if (!result) return result;
    ++selection.revision;
    return result;
}

Result<double> RTS::readUnitAttribute(Unit& unit, std::string_view attribute) const {
    if (!owns(units_, unit))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Unit does not belong to this facade", "unit"));
    return RTSUnitAttributeAdapter::read(unit, attribute);
}

Result<void> RTS::setUnitAttribute(Unit& unit, std::string_view attribute, double value) const {
    if (!owns(units_, unit))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Unit does not belong to this facade", "unit"));
    return RTSUnitAttributeAdapter::setBase(unit, attribute, value);
}

Result<effects::EffectHandle> RTS::applyEffect(Unit& unit, const RTSEffectDefinition& definition) const {
    if (!owns(units_, unit))
        return Result<effects::EffectHandle>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Unit does not belong to this facade", "unit"));
    return unit.effects()->values.apply(definition);
}

Result<effects::EffectHandle> RTS::applyEffect(Building& building, const RTSEffectDefinition& definition) const {
    if (!owns(buildings_, building))
        return Result<effects::EffectHandle>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Building does not belong to this facade", "building"));
    return building.effects()->values.apply(definition);
}

Result<RTSBuildReceipt> RTS::build(Building& building, action::ActionRuntime& action,
                                   resource::IResourceAccount& account, resource::CostSpec cost, std::string product,
                                   Duration duration, std::string productionKind, int priority,
                                   std::string transactionId) {
    if (!owns(buildings_, building))
        return Result<RTSBuildReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Building does not belong to this facade", "building"));
    return RTSProductionActionAdapter::build(building, action, account, std::move(cost), std::move(product),
                                             std::move(duration), std::move(productionKind), priority,
                                             std::move(transactionId));
}

Result<std::size_t> RTS::step(const SimulationStep& simulationStep, IRTSActionExecutor& executor) {
    auto motion = MotionSystem::step(simulationStep);
    if (!motion) return failureFrom<std::size_t>(motion.status());
    std::size_t processed = std::move(motion).takeValue();

    auto actions = OrderActionSystem::step(simulationStep, executor);
    if (!actions) return failureFrom<std::size_t>(actions.status());
    processed += std::move(actions).takeValue();

    auto production = BuildingProductionSystem::step(simulationStep);
    if (!production) return failureFrom<std::size_t>(production.status());
    processed += std::move(production).takeValue();

    auto effects = EffectSystem::step(simulationStep);
    if (!effects) return failureFrom<std::size_t>(effects.status());
    processed += std::move(effects).takeValue();
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

std::size_t RTS::unitCount() const noexcept { return countLive<Unit>(units_); }
std::size_t RTS::buildingCount() const noexcept { return countLive<Building>(buildings_); }
std::size_t RTS::playerCount() const noexcept { return countLive<Player>(players_); }
std::size_t RTS::factionCount() const noexcept { return countLive<Faction>(factions_); }

Player* RTS::resolvePlayer(SubjectRef subject) const noexcept {
    for (const auto& handle : players_) {
        auto* player = dynamic_cast<Player*>(ecs::try_get(handle));
        if (player && player->identity()->subject == subject) return player;
    }
    return nullptr;
}

Unit* RTS::resolveUnit(SubjectRef subject) const noexcept {
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit && unit->identity()->subject == subject) return unit;
    }
    return nullptr;
}

void RTS::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RTS::create, false);
    expose(cls);
}

void RTS::expose(ssq::Class& cls) {
    cls.addFunc("getName", &RTS::getName);
    // simplesquirrel's member-function binder predates noexcept member
    // pointers; keep the C++ query APIs noexcept and adapt them at the script
    // boundary with the same object-pointer convention used by other modules.
    cls.addFunc("unitCount", [](RTS* self) { return self->unitCount(); });
    cls.addFunc("buildingCount", [](RTS* self) { return self->buildingCount(); });
    cls.addFunc("playerCount", [](RTS* self) { return self->playerCount(); });
    cls.addFunc("factionCount", [](RTS* self) { return self->factionCount(); });
}

}  // namespace eve::rts
