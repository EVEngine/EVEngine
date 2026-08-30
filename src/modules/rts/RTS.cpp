#include "rts/RTS.h"
#include "rts/RTSAttributes.h"

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

RTS::~RTS() {
    destroyHandles<Unit>(units_);
    destroyHandles<Building>(buildings_);
    destroyHandles<Player>(players_);
    destroyHandles<Faction>(factions_);
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

Result<FanOutReceipt> RTS::fanOut(const Player::Selection& selection, const CommandSpec& command,
                                  const FormationSpec& formation) const {
    return CommandFanOutSystem::fanOut(selection.units, command, formation);
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
