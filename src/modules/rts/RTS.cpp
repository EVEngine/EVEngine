#include "rts/RTS.h"
#include "rts/RTSAttributes.h"
#include "common/SquirrelBinding.h"
#include "crowd/Crowd.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "map/Fov.h"
#include "map/Pathfinder.h"
#include "sensing/Sensing.h"
#include "weapon/WeaponDefinitionRuntime.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
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

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) noexcept {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

Result<RTSEffectDefinition> resolveEffectDefinition(
    definitions::DefinitionRegistry& registry, std::string_view id,
    SubjectRef source, double durationOverride = -1.0) {
    if (id.empty() || !source.isValid() || !std::isfinite(durationOverride))
        return failure<RTSEffectDefinition>(DiagnosticCode::InvalidArgument,
            "RTS status effect requires an id, valid source, and finite duration override", "effect");
    auto resolved = registry.resolve("effect", std::string(id));
    if (!resolved) return Result<RTSEffectDefinition>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<RTSEffectDefinition>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<RTSEffectDefinition>(DiagnosticCode::InvalidArgument,
                                            "RTS status effect definition must be an object", "effect");
    const auto number = [object](std::string_view name, double fallback) -> std::optional<double> {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return fallback;
        if (const auto* integer = found->second.getIf<std::int64_t>()) return static_cast<double>(*integer);
        if (const auto* real = found->second.getIf<double>()) return *real;
        return std::nullopt;
    };
    const auto duration = number("duration", 0.0);
    const auto speed = number("speedMultiplier", 1.0);
    const auto damage = number("damageMultiplier", 1.0);
    const auto incoming = number("incomingDamageMultiplier", 1.0);
    const auto healing = number("healingPerSecond", 0.0);
    if (!duration || !speed || !damage || !incoming || !healing ||
        !std::isfinite(*duration) || !std::isfinite(*speed) || !std::isfinite(*damage) ||
        !std::isfinite(*incoming) || !std::isfinite(*healing) || *duration < 0.0 ||
        *speed < 0.0 || *damage < 0.0 || *incoming < 0.0 || *healing < 0.0)
        return failure<RTSEffectDefinition>(DiagnosticCode::InvalidArgument,
            "RTS status effect duration and modifiers must be finite non-negative numbers", "effect");
    RTSEffectDefinition definition;
    definition.id = std::string(id);
    definition.source = source.format();
    definition.duration = durationOverride >= 0.0 ? durationOverride : *duration;
    definition.speedMultiplier = *speed;
    definition.damageMultiplier = *damage;
    definition.incomingDamageMultiplier = *incoming;
    definition.healingPerSecond = *healing;
    definition.tags = {"status:" + std::string(id)};
    return Result<RTSEffectDefinition>::success(std::move(definition),
                                                Status::success(StatusCode::Applied));
}

Result<resource::CostSpec> scaledCost(const resource::CostSpec& source, double factor) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return failure<resource::CostSpec>(DiagnosticCode::InvalidArgument,
            "RTS refund factor must be finite and positive", "refund.factor");
    std::vector<resource::ResourceCost> items;
    items.reserve(source.items().size());
    for (const auto& item : source.items()) {
        const auto amount = static_cast<std::int64_t>(std::llround(
            static_cast<double>(item.amount.value()) * factor));
        if (amount <= 0) continue;
        auto scaled = resource::ResourceCost::create(item.resource.value(), amount);
        if (!scaled) return Result<resource::CostSpec>::failure(scaled.status());
        items.push_back(std::move(scaled).takeValue());
    }
    return resource::CostSpec::create(std::move(items));
}

std::uint64_t stableRallyGroup(SubjectRef subject) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : subject.format()) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

SubjectRef deterministicSubject(std::string_view seed, std::uint64_t sequence) {
    const auto hash = [&](std::uint64_t basis) {
        std::uint64_t value = basis;
        for (unsigned char byte : seed) {
            value ^= byte;
            value *= 1099511628211ull;
        }
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value ^= static_cast<unsigned char>(sequence >> shift);
            value *= 1099511628211ull;
        }
        return value;
    };
    const std::uint64_t high = hash(1469598103934665603ull);
    const std::uint64_t low = hash(1099511628211ull);
    PersistentId::Bytes bytes{};
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::uint8_t>(high >> ((7 - index) * 8));
        bytes[8 + index] = static_cast<std::uint8_t>(low >> ((7 - index) * 8));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x70);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
    return SubjectRef::fromPersistentId(PersistentId(bytes));
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

template <typename T>
T* findSubject(const std::vector<ecs::EntityHandle>& handles, SubjectRef subject) {
    for (const auto& handle : handles) {
        auto* entity = dynamic_cast<T*>(ecs::try_get(handle));
        if (entity != nullptr && entity->identity()->subject == subject) return entity;
    }
    return nullptr;
}

Result<SubjectRef> parseScriptSubject(std::string_view text, std::string_view path) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed)
        return failure<SubjectRef>(DiagnosticCode::InvalidArgument,
                                   "RTS script identity must be a canonical UUID", std::string(path));
    return Result<SubjectRef>::success(SubjectRef::fromPersistentId(*parsed));
}

Result<LogicalId> parseScriptDefinition(std::string_view text) {
    if (text.empty()) return Result<LogicalId>::success(LogicalId{});
    const auto parsed = LogicalId::parse(text);
    if (!parsed)
        return failure<LogicalId>(DiagnosticCode::InvalidArgument,
                                  "RTS script definition must be a canonical logical id", "definition");
    return Result<LogicalId>::success(*parsed);
}

Result<std::vector<SubjectRef>> parseScriptSubjects(const std::vector<std::string>& texts) {
    std::vector<SubjectRef> subjects;
    subjects.reserve(texts.size());
    for (std::size_t index = 0; index < texts.size(); ++index) {
        auto subject = parseScriptSubject(texts[index], "subjects[" + std::to_string(index) + "]");
        if (!subject) return Result<std::vector<SubjectRef>>::failure(subject.status());
        subjects.push_back(std::move(subject).takeValue());
    }
    return Result<std::vector<SubjectRef>>::success(std::move(subjects));
}

Result<CombatStance> parseCombatStance(std::string_view text) {
    if (text == "passive") return Result<CombatStance>::success(CombatStance::Passive);
    if (text == "defensive") return Result<CombatStance>::success(CombatStance::Defensive);
    if (text == "aggressive") return Result<CombatStance>::success(CombatStance::Aggressive);
    return failure<CombatStance>(DiagnosticCode::InvalidArgument,
        "RTS combat stance must be passive, defensive, or aggressive", "stance");
}

Value fanOutValue(FanOutReceipt receipt) {
    Value::Array orderIds;
    orderIds.reserve(receipt.orderIds.size());
    for (auto& id : receipt.orderIds) orderIds.emplace_back(std::move(id));
    return Value(Value::Object{{"requested", static_cast<std::int64_t>(receipt.requested)},
                               {"accepted", static_cast<std::int64_t>(receipt.accepted)},
                               {"orderIds", Value(std::move(orderIds))}});
}

}  // namespace

Module_IMPL(RTS, new RTS());

struct RTS::ScriptRuntime {
    struct EconomySlot {
        economy::EconomyLedger ledger;
        economy::EconomyLedgerResourceAccount account{ledger};
    };

    struct PaidProduction {
        SubjectRef producer;
        SubjectRef resultSubject;
        std::string kind;
        std::string product;
        std::string taskId;
        std::string orderId;
        resource::CostSpec refund;
    };

    struct PaidConstruction {
        SubjectRef building;
        SubjectRef faction;
        resource::CostSpec cost;
    };

    struct Checkpoint {
        RTSStateSnapshot roots;
        std::map<std::string, economy::EconomyLedger::Snapshot> economies;
        std::map<std::string, map::Fov::Snapshot> fovs;
        std::map<std::string, SubjectRef> pendingProductionSubjects;
        std::vector<PaidProduction> paidProduction;
        std::vector<PaidConstruction> paidConstruction;
        RTSCommandLog commandLog;
        std::vector<float> navigationCosts;
        std::vector<float> terrainElevations;
        std::uint64_t nextTick = 1;
        std::uint64_t aiProductionSequence = 1;
    };

    action::ActionRuntime actions;
    ActionAdapter         adapter{actions};
    map::Pathfinder       pathfinder;
    crowd::Crowd          crowd;
    sensing::SensingWorld sensing;
    combat::DamageRuntime damage;
    definitions::DefinitionRegistry definitions;
    RTSCommandLog        commandLog;
    std::map<std::string, std::unique_ptr<EconomySlot>> economies;
    std::map<std::string, std::unique_ptr<map::Fov>> fovs;
    std::map<std::string, SubjectRef> pendingProductionSubjects;
    std::vector<PaidProduction> paidProduction;
    std::vector<PaidConstruction> paidConstruction;
    std::map<std::string, Checkpoint> checkpoints;
    std::uint64_t         nextTick = 1;
    std::uint64_t         aiProductionSequence = 1;
    bool                  spawningProduction = false;
    int                   width = 0;
    int                   height = 0;
    float                 cellSize = 1.0f;
    float                 originX = 0.0f;
    float                 originY = 0.0f;
    std::vector<float>    terrainElevations;
    bool                  configured = false;
};

RTS::RTS() = default;

RTS::~RTS() {
    FogOfWarSystem::clear(fogState_);
    setCrowdProvider(nullptr);
    setCombatProviders(nullptr, nullptr);
    clearOwnedRoots();
}

void RTS::clearOwnedRoots() noexcept {
    destroyHandles<Match>(matches_);
    destroyHandles<Unit>(units_);
    destroyHandles<Building>(buildings_);
    destroyHandles<ResourceNode>(resourceNodes_);
    destroyHandles<Player>(players_);
    destroyHandles<Faction>(factions_);
    destroyHandles<weapon::WeaponEntity>(weapons_);
}

void RTS::setFogProvider(FogProvider provider) noexcept {
    FogOfWarSystem::clear(fogState_);
    fogProvider_ = std::move(provider);
    for (const auto& handle : factions_)
        if (auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle)))
            faction->intel()->enabled = static_cast<bool>(fogProvider_);
}

void RTS::setCombatProviders(sensing::SensingWorld* sensing, combat::DamageRuntime* damage) noexcept {
    if (sensing_ != nullptr && (sensing_ != sensing || damage == nullptr)) {
        for (const auto& id : combatState_.mirroredSubjects)
            sensing_->remove(id).ignore("best-effort RTS sensing mirror cleanup");
        combatState_.mirroredSubjects.clear();
        combatState_.blockedSubjects.clear();
    }
    sensing_ = sensing;
    damage_ = damage;
}

void RTS::setCrowdProvider(crowd::Crowd* crowd) noexcept {
    if (crowd_ == crowd) return;
    if (crowd_ != nullptr) {
        for (const auto& handle : units_) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (unit != nullptr && unit->crowd()->link.isBound())
                crowd_->removeNamedAgent(unit->crowd()->link.key());
        }
    }

    crowd_ = crowd;
}

void RTS::setNavigationProvider(map::Pathfinder* pathfinder, NavigationGrid grid,
                                NavigationEvent unreachable) noexcept {
    pathfinder_ = pathfinder;
    navigationGrid_ = grid;
    navigationEvent_ = std::move(unreachable);
}

Result<Unit*> RTS::newUnit(SubjectRef subject, LogicalId definition) {
    if (!validSubject(subject))
        return failure<Unit*>(DiagnosticCode::InvalidArgument, "RTS Unit requires a valid SubjectRef", "subject");
    if (ownsSubject(subject))
        return failure<Unit*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module", "subject");
    Unit* unit = Unit::createUnit(subject, std::move(definition));
    if (unit == nullptr) return failure<Unit*>(DiagnosticCode::Failed, "ECS failed to create an RTS Unit", "unit");
    unit->attributes()->values = AttributeComponent{};
    if (!scriptRuntime_ || !scriptRuntime_->spawningProduction) {
        auto attributes = RTSUnitAttributeAdapter::ensure(*unit);
        if (!attributes) {
            const auto status = attributes.status();
            unit->release();
            return Result<Unit*>::failure(status);
        }
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
    if (ownsSubject(subject))
        return failure<Building*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module",
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

Result<ResourceNode*> RTS::newResourceNode(SubjectRef subject, std::string resourceType, float amount,
                                            WorldPosition position, std::size_t workerCapacity) {
    if (!validSubject(subject) || resourceType.empty() || !std::isfinite(amount) || amount < 0.0f ||
        !std::isfinite(position.x) || !std::isfinite(position.y) || workerCapacity == 0)
        return failure<ResourceNode*>(DiagnosticCode::InvalidArgument,
                                      "RTS resource node requires valid identity, stock, position and capacity",
                                      "resourceNode");
    if (ownsSubject(subject))
        return failure<ResourceNode*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module",
                                      "subject");
    ResourceNode* node = ResourceNode::createResourceNode(subject);
    if (node == nullptr)
        return failure<ResourceNode*>(DiagnosticCode::Failed, "ECS failed to create an RTS ResourceNode",
                                      "resourceNode");
    node->position()->x       = position.x;
    node->position()->y       = position.y;
    node->stock()->resourceType = std::move(resourceType);
    node->stock()->remaining  = amount;
    node->stock()->maximum    = amount;
    node->harvest()->capacity = workerCapacity;
    resourceNodes_.push_back(ecs::handle_of(node));
    return Result<ResourceNode*>::success(node, Status::success(StatusCode::Applied));
}

Result<Player*> RTS::newPlayer(SubjectRef subject) {
    if (!validSubject(subject))
        return failure<Player*>(DiagnosticCode::InvalidArgument, "RTS Player requires a valid SubjectRef", "subject");
    if (ownsSubject(subject))
        return failure<Player*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module", "subject");
    Player* player = Player::createPlayer(subject);
    if (player == nullptr)
        return failure<Player*>(DiagnosticCode::Failed, "ECS failed to create an RTS Player", "player");
    players_.push_back(ecs::handle_of(player));
    return Result<Player*>::success(player, Status::success(StatusCode::Applied));
}

Result<Faction*> RTS::newFaction(SubjectRef subject) {
    if (!validSubject(subject))
        return failure<Faction*>(DiagnosticCode::InvalidArgument, "RTS Faction requires a valid SubjectRef", "subject");
    if (ownsSubject(subject))
        return failure<Faction*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module", "subject");
    Faction* faction = Faction::createFaction(subject);
    if (faction == nullptr)
        return failure<Faction*>(DiagnosticCode::Failed, "ECS failed to create an RTS Faction", "faction");
    factions_.push_back(ecs::handle_of(faction));
    faction->intel()->enabled = static_cast<bool>(fogProvider_);
    if (scriptRuntime_ && scriptRuntime_->configured) {
        const std::string key = faction->identity()->subject.format();
        scriptRuntime_->economies.emplace(key, std::make_unique<ScriptRuntime::EconomySlot>());
        auto factionFov = std::make_unique<map::Fov>(scriptRuntime_->width, scriptRuntime_->height);
        factionFov->setMode("heightmap");
        factionFov->setEyeOffset(1.0f);
        factionFov->setCliffBlock(0.0f);
        for (int y = 0; y < scriptRuntime_->height; ++y)
            for (int x = 0; x < scriptRuntime_->width; ++x)
                if (!scriptRuntime_->terrainElevations.empty())
                    factionFov->setElevation(x, y, scriptRuntime_->terrainElevations[
                        static_cast<std::size_t>(y * scriptRuntime_->width + x)]);
        scriptRuntime_->fovs.emplace(key, std::move(factionFov));
        auto economyLink = EconomyLink::bind("rts/script/economy/" + key);
        if (!economyLink) {
            const Status status = economyLink.status();
            scriptRuntime_->economies.erase(key);
            scriptRuntime_->fovs.erase(key);
            faction->release();
            factions_.pop_back();
            return Result<Faction*>::failure(status);
        }
        faction->economy()->link = std::move(economyLink).takeValue();
    }
    return Result<Faction*>::success(faction, Status::success(StatusCode::Applied));
}

Result<void> RTS::materialize(Unit& unit) {
    if (definitions_ == nullptr || !unit.definition()->id.isValid())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    const std::size_t weaponCount = weapons_.size();
    ArchetypeWeaponFactory factory = [this](std::string_view definitionId,
                                             PersistentId instanceId) -> Result<weapon::WeaponEntity*> {
        auto runtime = weapon::WeaponDefinitionRuntime::create(*definitions_, definitionId, instanceId);
        if (!runtime) return Result<weapon::WeaponEntity*>::failure(runtime.status());
        auto* entity = weapon::WeaponEntity::createWeapon();
        if (entity == nullptr)
            return failure<weapon::WeaponEntity*>(DiagnosticCode::Failed,
                                                  "ECS failed to create an RTS weapon instance", "weapon");
        auto applied = runtime.value().applyTo(entity);
        if (!applied) {
            const Status status = applied.status();
            entity->release();
            return Result<weapon::WeaponEntity*>::failure(status);
        }
        weapons_.push_back(ecs::handle_of(entity));
        return Result<weapon::WeaponEntity*>::success(entity, Status::success(StatusCode::Applied));
    };
    auto applied = RTSArchetypeMaterializer::apply(*definitions_, unit, factory);
    if (!applied) {
        while (weapons_.size() > weaponCount) {
            if (auto* entity = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(weapons_.back()))) entity->release();
            weapons_.pop_back();
        }
    }
    return applied;
}

Result<void> RTS::materialize(Building& building) {
    if (definitions_ == nullptr || !building.definition()->id.isValid())
        return Result<void>::success(Status::success(StatusCode::NoOp));
    const std::size_t weaponCount = weapons_.size();
    ArchetypeWeaponFactory factory = [this](std::string_view definitionId,
                                             PersistentId instanceId) -> Result<weapon::WeaponEntity*> {
        auto runtime = weapon::WeaponDefinitionRuntime::create(*definitions_, definitionId, instanceId);
        if (!runtime) return Result<weapon::WeaponEntity*>::failure(runtime.status());
        auto* entity = weapon::WeaponEntity::createWeapon();
        if (entity == nullptr)
            return failure<weapon::WeaponEntity*>(DiagnosticCode::Failed,
                                                  "ECS failed to create an RTS weapon instance", "weapon");
        auto applied = runtime.value().applyTo(entity);
        if (!applied) {
            const Status status = applied.status();
            entity->release();
            return Result<weapon::WeaponEntity*>::failure(status);
        }
        weapons_.push_back(ecs::handle_of(entity));
        return Result<weapon::WeaponEntity*>::success(entity, Status::success(StatusCode::Applied));
    };
    auto applied = RTSArchetypeMaterializer::apply(*definitions_, building, factory);
    if (!applied) {
        while (weapons_.size() > weaponCount) {
            if (auto* entity = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(weapons_.back()))) entity->release();
            weapons_.pop_back();
        }
    }
    return applied;
}

Result<Unit*> RTS::newFactionUnit(Faction& faction, SubjectRef subject, LogicalId definition) {
    if (!owns(factions_, faction))
        return failure<Unit*>(DiagnosticCode::StaleHandle, "RTS Faction does not belong to this facade", "faction");
    auto created = newUnit(subject, std::move(definition));
    if (!created) return created;
    Unit* unit = std::move(created).takeValue();
    const std::size_t weaponCount = weapons_.size();
    const auto rollbackWeapons = [this, weaponCount]() {
        while (weapons_.size() > weaponCount) {
            if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(weapons_.back()))) weapon->release();
            weapons_.pop_back();
        }
    };
    auto archetype = materialize(*unit);
    if (!archetype) {
        const Status status = archetype.status();
        unit->release();
        units_.pop_back();
        rollbackWeapons();
        return Result<Unit*>::failure(status);
    }
    auto link = FactionLink::bind(ecs::handle_of(&faction));
    if (!link) {
        const Status status = link.status();
        unit->release();
        units_.pop_back();
        rollbackWeapons();
        return Result<Unit*>::failure(status);
    }
    unit->faction()->link = std::move(link).takeValue();
    faction.members()->units.push_back(ecs::handle_of(unit));
    if (scriptRuntime_ && scriptRuntime_->configured) {
        const std::string key = unit->identity()->subject.format();
        auto crowdLink = CrowdLink::bind(key);
        auto sensingLink = SensingLink::bind(key);
        if (!crowdLink || !sensingLink) {
            const Status status = !crowdLink ? crowdLink.status() : sensingLink.status();
            faction.members()->units.pop_back();
            unit->release();
            units_.pop_back();
            rollbackWeapons();
            return Result<Unit*>::failure(status);
        }
        unit->crowd()->link = std::move(crowdLink).takeValue();
        unit->sensing()->link = std::move(sensingLink).takeValue();
    }
    return Result<Unit*>::success(unit, Status::success(StatusCode::Applied));
}

Result<Building*> RTS::newFactionBuilding(Faction& faction, SubjectRef subject, LogicalId definition) {
    if (!owns(factions_, faction))
        return failure<Building*>(DiagnosticCode::StaleHandle, "RTS Faction does not belong to this facade",
                                  "faction");
    auto created = newBuilding(subject, std::move(definition));
    if (!created) return created;
    Building* building = std::move(created).takeValue();
    const std::size_t weaponCount = weapons_.size();
    const auto rollbackWeapons = [this, weaponCount]() {
        while (weapons_.size() > weaponCount) {
            if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(weapons_.back()))) weapon->release();
            weapons_.pop_back();
        }
    };
    auto archetype = materialize(*building);
    if (!archetype) {
        const Status status = archetype.status();
        building->release();
        buildings_.pop_back();
        rollbackWeapons();
        return Result<Building*>::failure(status);
    }
    auto link = FactionLink::bind(ecs::handle_of(&faction));
    if (!link) {
        const Status status = link.status();
        building->release();
        buildings_.pop_back();
        rollbackWeapons();
        return Result<Building*>::failure(status);
    }
    building->faction()->link = std::move(link).takeValue();
    faction.members()->buildings.push_back(ecs::handle_of(building));
    return Result<Building*>::success(building, Status::success(StatusCode::Applied));
}

Result<Match*> RTS::newMatch(SubjectRef subject) {
    if (!validSubject(subject))
        return failure<Match*>(DiagnosticCode::InvalidArgument, "RTS Match requires a valid SubjectRef", "subject");
    if (ownsSubject(subject))
        return failure<Match*>(DiagnosticCode::Conflict, "RTS SubjectRef is already owned by this module", "subject");
    Match* match = Match::createMatch(subject);
    if (match == nullptr) return failure<Match*>(DiagnosticCode::Failed, "ECS failed to create an RTS Match", "match");
    matches_.push_back(ecs::handle_of(match));
    return Result<Match*>::success(match, Status::success(StatusCode::Applied));
}

Result<void> RTS::configureMatch(Match& match, VictoryRule rule, std::string archetype,
                                 double targetValue) const {
    if (!owns(matches_, match))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS match is not owned by this composition root", "match");
    if (match.state()->phase != MatchPhase::Setup)
        return failure<void>(DiagnosticCode::Conflict, "RTS match rules are immutable after start", "match.phase");
    if (rule == VictoryRule::DestroyHeadquarters && archetype.empty())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS headquarters victory requires an archetype", "archetype");
    if (rule == VictoryRule::ResourceTarget &&
        (archetype.empty() || !std::isfinite(targetValue) || targetValue <= 0.0))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS resource victory requires a resource and positive target", "targetValue");
    match.rules()->rule = rule;
    match.rules()->archetype = std::move(archetype);
    match.rules()->targetValue = targetValue;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::addMatchParticipant(Match& match, Faction& faction, int team) const {
    if (!owns(matches_, match) || findFaction(faction.identity()->subject) != &faction)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS match and faction must share this composition root", "participant");
    return MatchSystem::addParticipant(match, faction, team);
}

Result<void> RTS::startMatch(Match& match) const {
    if (!owns(matches_, match))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS match is not owned by this composition root", "match");
    return MatchSystem::start(match);
}

Result<void> RTS::surrenderMatch(Match& match, Faction& faction) const {
    if (!owns(matches_, match) || findFaction(faction.identity()->subject) != &faction)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS match and faction must share this composition root", "participant");
    return MatchSystem::surrender(match, faction);
}

Result<Value> RTS::inspectMatch(Match& match) const {
    if (!owns(matches_, match))
        return failure<Value>(DiagnosticCode::InvalidArgument,
                              "RTS match is not owned by this composition root", "match");
    const auto phase = match.state()->phase == MatchPhase::Setup ? "setup" :
                       match.state()->phase == MatchPhase::Running ? "running" : "finished";
    Value::Array participants;
    for (auto& entry : match.participants()->entries) {
        auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
        participants.emplace_back(Value::Object{
            {"faction", faction == nullptr ? std::string{} : faction->identity()->subject.format()},
            {"team", entry.team}, {"eliminated", entry.eliminated},
            {"surrendered", entry.surrendered}, {"reason", entry.reason}});
    }
    Value::Array events;
    for (const auto& event : match.events()->values)
        events.emplace_back(Value::Object{{"sequence", static_cast<std::int64_t>(event.sequence)},
            {"kind", event.kind}, {"faction", event.faction.isValid() ? event.faction.format() : std::string{}},
            {"team", event.team}, {"reason", event.reason}});
    return Result<Value>::success(Value(Value::Object{
        {"subject", match.identity()->subject.format()}, {"phase", phase},
        {"winningTeam", match.state()->winningTeam},
        {"rule", static_cast<std::int64_t>(match.rules()->rule)},
        {"archetype", match.rules()->archetype}, {"target", match.rules()->targetValue},
        {"participants", Value(std::move(participants))}, {"events", Value(std::move(events))}}),
        Status::success(StatusCode::Applied));
}

Unit* RTS::findUnit(SubjectRef subject) const noexcept { return findSubject<Unit>(units_, subject); }

Building* RTS::findBuilding(SubjectRef subject) const noexcept { return findSubject<Building>(buildings_, subject); }

ResourceNode* RTS::findResourceNode(SubjectRef subject) const noexcept {
    return findSubject<ResourceNode>(resourceNodes_, subject);
}

bool RTS::ownsSubject(SubjectRef subject) const noexcept {
    return findSubject<Unit>(units_, subject) != nullptr || findSubject<Building>(buildings_, subject) != nullptr ||
           findSubject<ResourceNode>(resourceNodes_, subject) != nullptr ||
           findSubject<Player>(players_, subject) != nullptr || findSubject<Faction>(factions_, subject) != nullptr ||
           findSubject<Match>(matches_, subject) != nullptr;
}

Result<FanOutReceipt> RTS::fanOut(const Player::Selection& selection, const CommandSpec& command,
                                  const FormationSpec& formation) const {
    return CommandFanOutSystem::fanOut(selection.units, command, formation);
}

Result<FanOutReceipt> RTS::commandUnits(std::span<const SubjectRef> subjects, const CommandSpec& command,
                                        const FormationSpec& formation) const {
    Player::Selection selection;
    selection.units.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        Unit* unit = findUnit(subjects[index]);
        if (unit == nullptr) {
            return Result<FanOutReceipt>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS command unit identity was not found",
                "subjects[" + std::to_string(index) + "]"));
        }
        selection.units.push_back(ecs::handle_of(unit));
    }
    return fanOut(selection, command, formation);
}

Result<void> RTS::setUnitStance(std::span<const SubjectRef> subjects, CombatStance stance,
                                float leashRange) const {
    if (stance != CombatStance::Passive && stance != CombatStance::Defensive &&
        stance != CombatStance::Aggressive)
        return failure<void>(DiagnosticCode::InvalidArgument, "RTS combat stance is invalid", "stance");
    if (!std::isfinite(leashRange) || leashRange < 0.0f)
        return failure<void>(DiagnosticCode::InvalidArgument,
                            "RTS combat leash range must be finite and non-negative", "leashRange");
    std::vector<Unit*> units;
    units.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        auto* unit = findUnit(subjects[index]);
        if (unit == nullptr)
            return failure<void>(DiagnosticCode::NotFound, "RTS stance unit identity was not found",
                                 "subjects[" + std::to_string(index) + "]");
        units.push_back(unit);
    }
    for (auto* unit : units) {
        auto combat = unit->combat();
        combat->stance = stance;
        combat->leashRange = leashRange;
        combat->guardX = unit->motion()->x;
        combat->guardY = unit->motion()->y;
        combat->guardSet = true;
        if (stance == CombatStance::Passive && unit->orders()->values.orderCount() == 0)
            combat->target = {};
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setUnitMovementPriority(std::span<const SubjectRef> subjects, int priority) const {
    std::vector<Unit*> units;
    units.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        auto* unit = findUnit(subjects[index]);
        if (unit == nullptr)
            return failure<void>(DiagnosticCode::NotFound, "RTS movement-priority unit identity was not found",
                                 "subjects[" + std::to_string(index) + "]");
        units.push_back(unit);
    }
    const int clamped = std::clamp(priority, -100, 100);
    for (auto* unit : units) unit->navigation()->movementPriority = clamped;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::assignWorker(Unit& worker, ResourceNode& node, Building& dropoff) const {
    if (!owns(units_, worker) || !owns(resourceNodes_, node) || !owns(buildings_, dropoff))
        return failure<void>(DiagnosticCode::StaleHandle,
                            "RTS worker assignment requires roots owned by this facade", "assignment");
    if (!worker.durability()->alive || worker.durability()->state.health <= 0.0)
        return failure<void>(DiagnosticCode::Conflict, "RTS worker must be alive", "worker");
    if (worker.worker()->capacity <= 0.0f || worker.worker()->gatherRate <= 0.0f ||
        worker.worker()->resourceType.empty())
        return failure<void>(DiagnosticCode::Conflict,
                            "RTS unit is not configured as a resource worker", "worker");
    if ((!node.stock()->infinite && node.stock()->remaining <= 0.0f) ||
        node.stock()->resourceType != worker.worker()->resourceType)
        return failure<void>(DiagnosticCode::Conflict,
                            "RTS resource node is depleted or incompatible with the worker", "node");
    if (!dropoff.integrity()->alive || dropoff.construction()->progress < 1.0f ||
        worker.faction()->link.resolve() == nullptr ||
        worker.faction()->link.resolve() != dropoff.faction()->link.resolve() ||
        std::find(dropoff.dropoff()->acceptedResources.begin(), dropoff.dropoff()->acceptedResources.end(),
                  worker.worker()->resourceType) == dropoff.dropoff()->acceptedResources.end())
        return failure<void>(DiagnosticCode::Conflict,
                            "RTS dropoff must be completed, friendly, and accept the resource", "dropoff");

    auto& assigned = node.harvest()->workers;
    std::erase_if(assigned, [](const ecs::EntityHandle& handle) { return ecs::try_get(handle) == nullptr; });
    const auto workerHandle = ecs::handle_of(&worker);
    const bool alreadyAssigned = std::any_of(assigned.begin(), assigned.end(), [&](const auto& handle) {
        return sameHandle(handle, workerHandle);
    });
    if (!alreadyAssigned && assigned.size() >= node.harvest()->capacity)
        return failure<void>(DiagnosticCode::Conflict, "RTS resource node worker capacity is full", "node");

    auto nodeLink = ResourceNodeLink::bind(ecs::handle_of(&node));
    if (!nodeLink) return Result<void>::failure(nodeLink.status());
    auto dropoffLink = BuildingLink::bind(ecs::handle_of(&dropoff));
    if (!dropoffLink) return Result<void>::failure(dropoffLink.status());
    CommandSpec command;
    command.kind = OrderKind::Gather;
    command.target = {node.position()->x, node.position()->y};
    command.targetEntity = ecs::handle_of(&node);
    auto ordered = worker.orders()->values.replace(command);
    if (!ordered) return Result<void>::failure(ordered.status());
    std::move(ordered).takeValue();

    if (auto* previous = dynamic_cast<ResourceNode*>(worker.worker()->resourceNode.resolve());
        previous != nullptr && previous != &node) {
        std::erase_if(previous->harvest()->workers, [&](const auto& handle) {
            return sameHandle(handle, workerHandle);
        });
    }
    if (!alreadyAssigned) assigned.push_back(workerHandle);
    worker.worker()->resourceNode = std::move(nodeLink).takeValue();
    worker.worker()->dropoff = std::move(dropoffLink).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setWorkerAutoAssignment(std::span<const SubjectRef> subjects, bool enabled) const {
    std::vector<Unit*> workers;
    workers.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        auto* unit = findUnit(subjects[index]);
        if (unit == nullptr)
            return failure<void>(DiagnosticCode::NotFound,
                                 "RTS auto-assignment unit identity was not found",
                                 "subjects[" + std::to_string(index) + "]");
        if (unit->worker()->capacity <= 0.0f || unit->worker()->gatherRate <= 0.0f ||
            unit->worker()->resourceType.empty())
            return failure<void>(DiagnosticCode::Conflict,
                                 "RTS auto-assignment target is not a resource worker",
                                 "subjects[" + std::to_string(index) + "]");
        workers.push_back(unit);
    }
    for (auto* worker : workers) worker->worker()->autoAssign = enabled;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::configureWorkforce(Faction& faction, bool autoConstruction,
                                     int maxBuildersPerSite, bool autoRepair,
                                     int maxRepairersPerBuilding, int reserveWorkers) const {
    if (!owns(factions_, faction))
        return failure<void>(DiagnosticCode::StaleHandle,
                             "RTS workforce faction does not belong to this facade", "faction");
    if (maxBuildersPerSite <= 0 || maxRepairersPerBuilding <= 0 || reserveWorkers < 0)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS workforce limits must be positive and reserve workers non-negative",
                             "workforce");
    auto workforce = faction.workforce();
    workforce->autoConstruction = autoConstruction;
    workforce->autoRepair = autoRepair;
    workforce->maxBuildersPerSite = static_cast<std::size_t>(maxBuildersPerSite);
    workforce->maxRepairersPerBuilding = static_cast<std::size_t>(maxRepairersPerBuilding);
    workforce->reserveWorkers = static_cast<std::size_t>(reserveWorkers);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::string> RTS::assignBuilder(Unit& worker, Building& building) const {
    if (!owns(units_, worker) || !owns(buildings_, building))
        return failure<std::string>(DiagnosticCode::StaleHandle,
                                    "RTS builder assignment requires roots owned by this facade", "assignment");
    if (!worker.durability()->alive || worker.containment()->container.isBound() ||
        worker.worker()->buildRate <= 0.0f)
        return failure<std::string>(DiagnosticCode::Conflict,
                                    "RTS builder must be alive, deployed, and construction-capable", "worker");
    if (!building.integrity()->alive || building.construction()->progress >= 1.0f ||
        building.construction()->paused)
        return failure<std::string>(DiagnosticCode::Conflict,
                                    "RTS construction target must be alive, unfinished, and active", "building");
    if (worker.faction()->link.resolve() == nullptr ||
        worker.faction()->link.resolve() != building.faction()->link.resolve())
        return failure<std::string>(DiagnosticCode::Conflict,
                                    "RTS builder and construction target must share a faction", "assignment");
    CommandSpec command;
    command.kind = OrderKind::Build;
    command.target = {building.placement()->worldX, building.placement()->worldY};
    command.targetEntity = building.identity()->self;
    auto ordered = worker.orders()->values.replace(command);
    if (!ordered) return Result<std::string>::failure(ordered.status());
    return ordered;
}

Result<int> RTS::addUnitReserveAmmo(Unit& unit, int rounds) const {
    if (!owns(units_, unit))
        return failure<int>(DiagnosticCode::StaleHandle,
                            "RTS reserve-ammunition unit does not belong to this facade", "unit");
    if (!unit.durability()->alive)
        return failure<int>(DiagnosticCode::Conflict, "RTS reserve-ammunition unit must be alive", "unit");
    auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(unit.weapon()->link.resolve());
    if (weaponEntity == nullptr)
        return failure<int>(DiagnosticCode::NotFound, "RTS unit has no canonical weapon", "unit.weapon");
    auto& resource = weaponEntity->state()->resource;
    if (resource.kind != weapon::ResourceKind::Ammo || resource.infinite)
        return failure<int>(DiagnosticCode::Conflict,
                            "RTS unit weapon has no finite reserve ammunition", "unit.weapon.resource");
    if (auto* pool = weaponEntity->state()->ammoPool) {
        if (pool->state()->max < 0)
            return failure<int>(DiagnosticCode::Conflict,
                                "RTS unit weapon ammunition pool is infinite", "unit.weapon.ammoPool");
        const long long adjusted = static_cast<long long>(pool->state()->count) + rounds;
        pool->state()->count = static_cast<int>(std::clamp<long long>(adjusted, 0, pool->state()->max));
        return Result<int>::success(pool->state()->count, Status::success(StatusCode::Applied));
    }
    if (resource.reserve < 0)
        return failure<int>(DiagnosticCode::Conflict,
                            "RTS unit weapon reserve ammunition is infinite", "unit.weapon.resource.reserve");
    const auto* definition = weaponEntity->definition()->def;
    const int capacity = std::max(resource.reserve, definition == nullptr ? resource.reserve : definition->reserveSize);
    const long long adjusted = static_cast<long long>(resource.reserve) + rounds;
    resource.reserve = static_cast<int>(std::clamp<long long>(adjusted, 0, capacity));
    return Result<int>::success(resource.reserve, Status::success(StatusCode::Applied));
}

Result<float> RTS::addUnitAmmoSupply(Unit& unit, float rounds) const {
    if (!owns(units_, unit))
        return failure<float>(DiagnosticCode::StaleHandle,
                              "RTS ammunition-supply unit does not belong to this facade", "unit");
    if (!unit.durability()->alive)
        return failure<float>(DiagnosticCode::Conflict, "RTS ammunition-supply unit must be alive", "unit");
    if (!std::isfinite(rounds))
        return failure<float>(DiagnosticCode::InvalidArgument,
                              "RTS ammunition supply adjustment must be finite", "rounds");
    auto supply = unit.supply();
    supply->stock = std::clamp(supply->stock + rounds, 0.0f, std::max(0.0f, supply->capacity));
    return Result<float>::success(supply->stock, Status::success(StatusCode::Applied));
}

Result<float> RTS::addBuildingAmmoSupply(Building& building, float rounds) const {
    if (!owns(buildings_, building))
        return failure<float>(DiagnosticCode::StaleHandle,
                              "RTS ammunition-supply building does not belong to this facade", "building");
    if (!building.integrity()->alive)
        return failure<float>(DiagnosticCode::Conflict, "RTS ammunition-supply building must be alive", "building");
    if (!std::isfinite(rounds))
        return failure<float>(DiagnosticCode::InvalidArgument,
                              "RTS ammunition supply adjustment must be finite", "rounds");
    auto supply = building.supply();
    supply->stock = std::clamp(supply->stock + rounds, 0.0f, std::max(0.0f, supply->capacity));
    return Result<float>::success(supply->stock, Status::success(StatusCode::Applied));
}

Result<void> RTS::setUnitAutoResupply(Unit& unit, bool enabled) const {
    if (!owns(units_, unit))
        return failure<void>(DiagnosticCode::StaleHandle,
                             "RTS automatic-resupply unit does not belong to this facade", "unit");
    if (!unit.durability()->alive)
        return failure<void>(DiagnosticCode::Conflict, "RTS automatic-resupply unit must be alive", "unit");
    auto supply = unit.supply();
    supply->autoDispatch = enabled;
    if (enabled) return Result<void>::success(Status::success(StatusCode::Applied));
    auto current = unit.orders()->values.current();
    if (current && (current.value().kind == OrderKind::Resupply || current.value().kind == OrderKind::SupplyRelay)) {
        auto cancelled = unit.orders()->values.cancel(current.value().id, "automatic resupply disabled");
        if (!cancelled) return cancelled;
        supply->assignedTarget = {};
        supply->reservedStock = 0.0f;
        supply->returning = false;
        supply->rendezvousActive = false;
        supply->convoyWaiting = false;
        supply->convoyLeader = {};
        auto navigation = unit.navigation();
        navigation->waypoints.clear();
        navigation->waypointIndex = 0;
        navigation->plannedOrderId.clear();
        navigation->unreachable = false;
        navigation->unreachableReported = false;
        unit.motion()->arrived = true;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<FanOutReceipt> RTS::suppressArea(std::span<const SubjectRef> subjects,
                                        WorldPosition start, WorldPosition end,
                                        float width, int shotsPerUnit) const {
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) ||
        !std::isfinite(end.y) || !std::isfinite(width) || width <= 0.0f || shotsPerUnit < 0 ||
        std::hypot(end.x - start.x, end.y - start.y) <= 1e-3f)
        return failure<FanOutReceipt>(DiagnosticCode::InvalidArgument,
            "RTS suppression requires a non-degenerate corridor, positive width, and non-negative shots",
            "suppression");
    std::vector<Unit*> units;
    units.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        auto* unit = findUnit(subjects[index]);
        if (unit == nullptr)
            return failure<FanOutReceipt>(DiagnosticCode::NotFound,
                "RTS suppression unit identity was not found", "subjects[" + std::to_string(index) + "]");
        if (!unit->durability()->alive || unit->containment()->container.isBound() ||
            unit->weapon()->link.resolve() == nullptr)
            return failure<FanOutReceipt>(DiagnosticCode::Conflict,
                "RTS suppression unit must be alive, deployed, and armed",
                "subjects[" + std::to_string(index) + "]");
        units.push_back(unit);
    }
    CommandSpec command;
    command.kind = OrderKind::SuppressArea;
    command.target = start;
    command.secondaryTarget = end;
    command.radius = width;
    auto issued = commandUnits(subjects, command);
    if (!issued) return issued;
    for (auto* unit : units) {
        unit->artillery()->suppressionShotsRemaining = shotsPerUnit == 0 ? -1 : shotsPerUnit;
        unit->artillery()->fireSupportRequester = {};
    }
    return issued;
}

Result<FanOutReceipt> RTS::escortUnits(std::span<const SubjectRef> subjects,
                                       SubjectRef protectedSubject,
                                       float guardRadius, float spacing) const {
    if (!std::isfinite(guardRadius) || !std::isfinite(spacing) || guardRadius <= 0.0f || spacing <= 0.0f)
        return failure<FanOutReceipt>(DiagnosticCode::InvalidArgument,
            "RTS escort guard radius and spacing must be finite and positive", "escort");
    ecs::Entity* protectedEntity = findUnit(protectedSubject);
    if (protectedEntity == nullptr) protectedEntity = findBuilding(protectedSubject);
    ecs::Entity* protectedFaction = nullptr;
    WorldPosition center{};
    float protectedRadius = 0.0f;
    if (auto* unit = dynamic_cast<Unit*>(protectedEntity);
        unit != nullptr && unit->durability()->alive && !unit->containment()->container.isBound()) {
        protectedFaction = unit->faction()->link.resolve();
        center = {unit->motion()->x, unit->motion()->y};
        protectedRadius = unit->crowd()->radius;
    } else if (auto* building = dynamic_cast<Building*>(protectedEntity);
               building != nullptr && building->integrity()->alive) {
        protectedFaction = building->faction()->link.resolve();
        center = {building->placement()->worldX, building->placement()->worldY};
    } else {
        return failure<FanOutReceipt>(DiagnosticCode::NotFound,
            "RTS escort target must be a live deployed unit or building", "protectedSubject");
    }
    if (protectedFaction == nullptr)
        return failure<FanOutReceipt>(DiagnosticCode::Conflict,
            "RTS escort target must have an owning faction", "protectedSubject");

    std::vector<Unit*> escorts;
    escorts.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        auto* unit = findUnit(subjects[index]);
        if (unit == nullptr)
            return failure<FanOutReceipt>(DiagnosticCode::NotFound,
                "RTS escort unit identity was not found", "subjects[" + std::to_string(index) + "]");
        if (unit == protectedEntity || !unit->durability()->alive ||
            unit->containment()->container.isBound() || !FactionRelationSystem::allied(
                dynamic_cast<Faction*>(unit->faction()->link.resolve()),
                dynamic_cast<Faction*>(protectedFaction)))
            return failure<FanOutReceipt>(DiagnosticCode::Conflict,
                "RTS escort units must be live, deployed, friendly, and distinct from the target",
                "subjects[" + std::to_string(index) + "]");
        escorts.push_back(unit);
    }
    std::sort(escorts.begin(), escorts.end(), [](Unit* left, Unit* right) {
        return left->identity()->subject.format() < right->identity()->subject.format();
    });
    std::vector<SubjectRef> orderedSubjects;
    orderedSubjects.reserve(escorts.size());
    for (auto* unit : escorts) orderedSubjects.push_back(unit->identity()->subject);
    CommandSpec command;
    command.kind = OrderKind::Escort;
    command.target = center;
    command.targetEntity = ecs::handle_of(protectedEntity);
    auto issued = commandUnits(orderedSubjects, command);
    if (!issued) return issued;
    constexpr float tau = 2.0f * static_cast<float>(std::numbers::pi);
    for (std::size_t index = 0; index < escorts.size(); ++index) {
        const float angle = tau * static_cast<float>(index) / static_cast<float>(escorts.size());
        const float distance = protectedRadius + escorts[index]->crowd()->radius + spacing;
        escorts[index]->tactics()->escortOffsetX = std::cos(angle) * distance;
        escorts[index]->tactics()->escortOffsetY = std::sin(angle) * distance;
        escorts[index]->tactics()->protectionRange = guardRadius;
        escorts[index]->combat()->leashRange = guardRadius;
    }
    return issued;
}

Value RTS::inspectState() const {
    Value::Array units;
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr) continue;
        std::string faction;
        if (auto* owner = dynamic_cast<Faction*>(unit->faction()->link.resolve()); owner != nullptr)
            faction = owner->identity()->subject.format();
        std::string order = "idle";
        auto current = unit->orders()->values.current();
        if (current) order = orderKindName(std::move(current).takeValue().kind);
        auto* resourceNode = dynamic_cast<ResourceNode*>(unit->worker()->resourceNode.resolve());
        auto* dropoff = dynamic_cast<Building*>(unit->worker()->dropoff.resolve());
        int reserveAmmo = 0;
        int reserveAmmoCapacity = 0;
        if (auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(unit->weapon()->link.resolve())) {
            const auto& resource = weaponEntity->state()->resource;
            if (resource.kind == weapon::ResourceKind::Ammo && !resource.infinite) {
                if (auto* pool = weaponEntity->state()->ammoPool) {
                    reserveAmmo = pool->state()->count;
                    reserveAmmoCapacity = pool->state()->max;
                } else {
                    reserveAmmo = resource.reserve;
                    reserveAmmoCapacity = weaponEntity->definition()->def == nullptr
                        ? resource.reserve : weaponEntity->definition()->def->reserveSize;
                }
            }
        }
        units.emplace_back(Value::Object{
            {"subject", unit->identity()->subject.format()},
            {"definition", unit->definition()->id.format()},
            {"faction", std::move(faction)},
            {"x", unit->motion()->x},
            {"y", unit->motion()->y},
            {"airborne", unit->motion()->airborne},
            {"order", std::move(order)},
            {"queuedOrders", static_cast<std::int64_t>(unit->orders()->values.orderCount())},
            {"stance", combatStanceName(unit->combat()->stance)},
            {"leashRange", unit->combat()->leashRange},
            {"movementPriority", unit->navigation()->movementPriority},
            {"trafficWaiting", unit->navigation()->trafficWaiting},
            {"cloaked", unit->vision()->cloaked},
            {"health", unit->durability()->state.health},
            {"maxHealth", unit->durability()->state.maxHealth},
            {"shield", unit->shield()->value},
            {"maxShield", unit->shield()->capacity},
            {"experience", unit->veterancy()->experience},
            {"veterancyLevel", static_cast<std::int64_t>(unit->veterancy()->level)},
            {"activeEffects", static_cast<std::int64_t>(unit->effects()->values.count())},
            {"inCommand", unit->command()->inCommand},
            {"garrisoned", unit->containment()->container.resolve() != nullptr},
            {"resourceType", unit->worker()->resourceType},
            {"cargo", unit->worker()->cargo},
            {"cargoCapacity", unit->worker()->capacity},
            {"autoAssign", unit->worker()->autoAssign},
            {"resourceNode", resourceNode == nullptr ? std::string{} : resourceNode->identity()->subject.format()},
            {"dropoff", dropoff == nullptr ? std::string{} : dropoff->identity()->subject.format()},
            {"reserveAmmo", static_cast<std::int64_t>(reserveAmmo)},
            {"reserveAmmoCapacity", static_cast<std::int64_t>(reserveAmmoCapacity)},
            {"supplyStock", unit->supply()->stock},
            {"supplyCapacity", unit->supply()->capacity},
            {"autoResupply", unit->supply()->autoDispatch},
            {"reservedSupply", unit->supply()->reservedStock},
            {"supplyReturning", unit->supply()->returning},
        });
    }

    Value::Array buildings;
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr) continue;
        std::string faction;
        if (auto* owner = dynamic_cast<Faction*>(building->faction()->link.resolve()); owner != nullptr)
            faction = owner->identity()->subject.format();
        buildings.emplace_back(Value::Object{
            {"subject", building->identity()->subject.format()},
            {"definition", building->definition()->id.format()},
            {"faction", std::move(faction)},
            {"x", building->placement()->worldX},
            {"y", building->placement()->worldY},
            {"complete", building->construction()->progress >= 1.0f},
            {"progress", building->construction()->progress},
            {"powered", building->infrastructure()->powered},
            {"productionQueue", static_cast<std::int64_t>(building->production()->values.taskCount())},
            {"health", building->integrity()->state.health},
            {"maxHealth", building->integrity()->state.maxHealth},
            {"shield", building->shield()->value},
            {"maxShield", building->shield()->capacity},
            {"activeEffects", static_cast<std::int64_t>(building->effects()->values.count())},
            {"supplyStock", building->supply()->stock},
            {"supplyCapacity", building->supply()->capacity},
        });
    }

    Value::Array resourceNodes;
    for (const auto& handle : resourceNodes_) {
        auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(handle));
        if (node == nullptr) continue;
        resourceNodes.emplace_back(Value::Object{
            {"subject", node->identity()->subject.format()},
            {"resource", node->stock()->resourceType},
            {"remaining", node->stock()->remaining},
            {"maximum", node->stock()->maximum},
            {"x", node->position()->x},
            {"y", node->position()->y},
            {"capacity", static_cast<std::int64_t>(node->harvest()->capacity)},
            {"workers", static_cast<std::int64_t>(node->harvest()->workers.size())},
        });
    }

    Value::Array factions;
    for (const auto& handle : factions_) {
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (faction == nullptr) continue;
        factions.emplace_back(Value::Object{
            {"subject", faction->identity()->subject.format()},
            {"displayName", faction->identity()->displayName},
            {"units", static_cast<std::int64_t>(faction->members()->units.size())},
            {"buildings", static_cast<std::int64_t>(faction->members()->buildings.size())},
            {"autoConstruction", faction->workforce()->autoConstruction},
            {"autoRepair", faction->workforce()->autoRepair},
            {"maxBuildersPerSite", static_cast<std::int64_t>(faction->workforce()->maxBuildersPerSite)},
            {"maxRepairersPerBuilding",
             static_cast<std::int64_t>(faction->workforce()->maxRepairersPerBuilding)},
            {"reserveWorkers", static_cast<std::int64_t>(faction->workforce()->reserveWorkers)},
        });
    }

    return Value(Value::Object{
        {"units", Value(std::move(units))},
        {"buildings", Value(std::move(buildings))},
        {"resourceNodes", Value(std::move(resourceNodes))},
        {"factions", Value(std::move(factions))},
    });
}

Value RTS::inspectFrameEvents() const {
    const auto channelName = [](DamageChannel channel) {
        switch (channel) {
            case DamageChannel::Ability: return "ability";
            case DamageChannel::Projectile: return "projectile";
            case DamageChannel::Weapon: return "weapon";
        }
        return "weapon";
    };
    const auto reactionName = [](combat::HitReaction reaction) {
        switch (reaction) {
            case combat::HitReaction::None: return "none";
            case combat::HitReaction::Flinch: return "flinch";
            case combat::HitReaction::Stagger: return "stagger";
            case combat::HitReaction::Knockdown: return "knockdown";
            case combat::HitReaction::Death: return "death";
        }
        return "none";
    };
    std::vector<std::pair<std::uint64_t, Value>> sequenced;
    sequenced.reserve(frameDamageEvents_.size() + frameCombatEvents_.size() + frameLifecycleEvents_.size());
    for (const auto& event : frameDamageEvents_) {
        sequenced.emplace_back(event.sequence, Value(Value::Object{
            {"type", "damage"},
            {"tick", static_cast<std::int64_t>(event.tick.value())},
            {"channel", channelName(event.channel)},
            {"source", event.outcome.source.format()},
            {"target", event.outcome.target.format()},
            {"damageType", event.request.damageType},
            {"previousHealth", event.outcome.previousHealth},
            {"health", event.outcome.health},
            {"appliedHealthDamage", event.outcome.appliedHealthDamage},
            {"appliedPoiseDamage", event.outcome.appliedPoiseDamage},
            {"reaction", reactionName(event.outcome.reaction)},
            {"killed", event.outcome.reaction == combat::HitReaction::Death},
        }));
    }
    const auto fireType = [](CombatFireEventKind kind) {
        switch (kind) {
            case CombatFireEventKind::WeaponFired: return "weapon_fired";
            case CombatFireEventKind::ProjectileFired: return "projectile_fired";
            case CombatFireEventKind::ShotMissed: return "shot_missed";
            case CombatFireEventKind::ReloadStarted: return "reload_started";
            case CombatFireEventKind::ReloadCompleted: return "reload_completed";
            case CombatFireEventKind::WeaponDry: return "weapon_dry";
            case CombatFireEventKind::FireBlocked: return "fire_blocked";
        }
        return "weapon_fired";
    };
    for (const auto& value : frameCombatEvents_)
        sequenced.emplace_back(value.sequence, Value(Value::Object{
            {"type", fireType(value.event.kind)},
            {"tick", static_cast<std::int64_t>(value.tick.value())},
            {"source", value.event.source.format()},
            {"target", value.event.target.format()},
            {"x", value.event.point.x}, {"y", value.event.point.y},
        }));
    const auto lifecycleType = [](LifecycleEventKind kind) {
        switch (kind) {
            case LifecycleEventKind::SuppressionRecovered: return "suppression_recovered";
            case LifecycleEventKind::ShieldRecharged: return "shield_recharged";
            case LifecycleEventKind::ConstructionCompleted: return "construction_completed";
            case LifecycleEventKind::ProductionSpawnBlocked: return "production_spawn_blocked";
            case LifecycleEventKind::ProductionSpawnCleared: return "production_spawn_cleared";
            case LifecycleEventKind::UnitProduced: return "unit_produced";
            case LifecycleEventKind::BuildingCaptured: return "building_captured";
            case LifecycleEventKind::AbilityCast: return "ability_cast";
            case LifecycleEventKind::AbilityChannelStarted: return "ability_channel_started";
            case LifecycleEventKind::AbilityChannelTick: return "ability_channel_tick";
            case LifecycleEventKind::AbilityChannelCompleted: return "ability_channel_completed";
            case LifecycleEventKind::AbilityInterrupted: return "ability_interrupted";
            case LifecycleEventKind::AbilityChannelCancelled: return "ability_channel_cancelled";
            case LifecycleEventKind::StatusApplied: return "status_applied";
            case LifecycleEventKind::StatusExpired: return "status_expired";
            case LifecycleEventKind::AmmoProduced: return "ammo_produced";
            case LifecycleEventKind::SupplyDispatched: return "supply_dispatched";
            case LifecycleEventKind::SupplyRelayDispatched: return "supply_rendezvous_dispatched";
            case LifecycleEventKind::AmmoResupplied: return "ammo_resupplied";
            case LifecycleEventKind::SupplyRelayTransferred: return "supply_relay_transferred";
            case LifecycleEventKind::SupplyReturning: return "supply_returning";
            case LifecycleEventKind::SupplyReturned: return "supply_returned";
        }
        return "lifecycle";
    };
    for (const auto& value : frameLifecycleEvents_)
        sequenced.emplace_back(value.sequence, Value(Value::Object{
            {"type", lifecycleType(value.event.kind)},
            {"tick", static_cast<std::int64_t>(value.tick.value())},
            {"source", value.event.source.format()},
            {"target", value.event.target.format()},
            {"detail", value.event.detail},
            {"value", value.event.value},
        }));
    std::sort(sequenced.begin(), sequenced.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    Value::Array events;
    events.reserve(sequenced.size());
    for (auto& [sequence, value] : sequenced) {
        (void)sequence;
        events.push_back(std::move(value));
    }
    return Value(std::move(events));
}

void RTS::recordDamageEvent(const combat::DamageRequest& request,
                            const combat::DamageOutcome& outcome,
                            SimulationTick tick, DamageChannel channel) {
    frameDamageEvents_.push_back({frameEventSequence_++, tick, channel, request, outcome});
}

void RTS::recordCombatEvent(const CombatFireEvent& event, SimulationTick tick) {
    frameCombatEvents_.push_back({frameEventSequence_++, tick, event});
}

void RTS::recordLifecycleEvent(const LifecycleEvent& event, SimulationTick tick) {
    frameLifecycleEvents_.push_back({frameEventSequence_++, tick, event});
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

Result<double> RTS::heal(SubjectRef source, SubjectRef target, double amount) const {
    if (!source.isValid() || !target.isValid() || !std::isfinite(amount) || amount <= 0.0)
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "RTS healing requires valid subjects and a finite positive amount", "healing");
    if (!ownsSubject(source))
        return failure<double>(DiagnosticCode::NotFound, "RTS healing source was not found", "source");
    combat::CombatState* state = nullptr;
    bool* alive = nullptr;
    if (auto* unit = findUnit(target)) {
        state = &unit->durability()->state;
        alive = &unit->durability()->alive;
    } else if (auto* building = findBuilding(target)) {
        state = &building->integrity()->state;
        alive = &building->integrity()->alive;
    } else {
        return failure<double>(DiagnosticCode::NotFound, "RTS healing target was not found", "target");
    }
    if (!*alive || state->health <= 0.0)
        return failure<double>(DiagnosticCode::Conflict, "RTS healing target must be alive", "target");
    auto valid = state->validate();
    if (!valid) return Result<double>::failure(valid.status());
    const double applied = std::min(amount, state->maxHealth - state->health);
    state->health += applied;
    return Result<double>::success(applied,
        Status::success(applied == 0.0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<effects::EffectHandle> RTS::applyStatusEffect(
    SubjectRef source, SubjectRef target, std::string effect, double durationOverride) {
    if (definitions_ == nullptr)
        return failure<effects::EffectHandle>(DiagnosticCode::Conflict,
                                               "RTS status effects require a definition registry", "definitions");
    if (!ownsSubject(source))
        return failure<effects::EffectHandle>(DiagnosticCode::NotFound,
                                               "RTS status-effect source was not found", "source");
    auto definition = resolveEffectDefinition(*definitions_, effect, source, durationOverride);
    if (!definition) return Result<effects::EffectHandle>::failure(definition.status());
    if (auto* unit = findUnit(target)) {
        if (!unit->durability()->alive)
            return failure<effects::EffectHandle>(DiagnosticCode::Conflict,
                                                   "RTS status-effect target must be alive", "target");
        auto applied = applyEffect(*unit, definition.value());
        if (applied)
            recordLifecycleEvent({LifecycleEventKind::StatusApplied, source, target, effect,
                                  definition.value().duration}, SimulationTick(scriptTick()));
        return applied;
    }
    if (auto* building = findBuilding(target)) {
        if (!building->integrity()->alive)
            return failure<effects::EffectHandle>(DiagnosticCode::Conflict,
                                                   "RTS status-effect target must be alive", "target");
        auto applied = applyEffect(*building, definition.value());
        if (applied)
            recordLifecycleEvent({LifecycleEventKind::StatusApplied, source, target, effect,
                                  definition.value().duration}, SimulationTick(scriptTick()));
        return applied;
    }
    return failure<effects::EffectHandle>(DiagnosticCode::NotFound,
                                           "RTS status-effect target was not found", "target");
}

Result<RTSBuildReceipt> RTS::build(Building& building, action::ActionRuntime& action,
                                   resource::IResourceAccount& account, resource::CostSpec cost, std::string product,
                                   Duration duration, std::string productionKind, int priority,
                                   std::string transactionId) {
    if (!owns(buildings_, building))
        return Result<RTSBuildReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "RTS Building does not belong to this facade", "building"));
    std::vector<RTSProductionResourceReserve> reserves;
    if (auto* faction = dynamic_cast<Faction*>(building.faction()->link.resolve()); faction != nullptr) {
        reserves.reserve(faction->productionPolicy()->resourceReserves.size());
        for (const auto& [resource, policy] : faction->productionPolicy()->resourceReserves) {
            auto item = resource::ResourceCost::create(resource, policy.amount);
            if (!item) return Result<RTSBuildReceipt>::failure(item.status());
            reserves.push_back({std::move(item).takeValue(), policy.minimumPriority});
        }
    }
    return RTSProductionActionAdapter::build(building, action, account, std::move(cost), std::move(product),
                                             std::move(duration), std::move(productionKind), priority,
                                             std::move(transactionId), std::move(reserves));
}

Result<void> RTS::setProductionResourceReserve(
    Faction& faction, std::string resource, std::int64_t amount, int minimumPriority) {
    if (!owns(factions_, faction))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::StaleHandle, "RTS Faction does not belong to this facade", "faction"));
    if (resource.empty() || amount < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "RTS production resource reserve requires a resource and non-negative amount", "reserve"));
    if (amount == 0) {
        faction.productionPolicy()->resourceReserves.erase(resource);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    auto valid = resource::ResourceCost::create(resource, amount);
    if (!valid) return Result<void>::failure(valid.status());
    faction.productionPolicy()->resourceReserves[std::move(resource)] = {amount, minimumPriority};
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<RTSCancelProductionReceipt> RTS::cancelProduction(
    Building& building, resource::IResourceAccount& account, std::string productionTaskId,
    std::string orderId, resource::CostSpec refund, std::string reason) {
    if (!owns(buildings_, building))
        return Result<RTSCancelProductionReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle,
                "RTS Building does not belong to this facade", "building"));
    return RTSProductionActionAdapter::cancel(building, account, std::move(productionTaskId),
        std::move(orderId), std::move(refund), std::move(reason));
}

Result<std::size_t> RTS::step(const SimulationStep& simulationStep, IRTSActionExecutor& executor) {
    if (!preserveFrameEventsOnNextStep_) {
        frameDamageEvents_.clear();
        frameCombatEvents_.clear();
        frameLifecycleEvents_.clear();
        frameEventSequence_ = 0;
    }
    preserveFrameEventsOnNextStep_ = false;
    const DamageEventSink damageEvents = [this](const combat::DamageRequest& request,
                                                 const combat::DamageOutcome& outcome,
                                                 SimulationTick tick, DamageChannel channel) {
        recordDamageEvent(request, outcome, tick, channel);
    };
    const CombatFireEventSink fireEvents = [this](const CombatFireEvent& event, SimulationTick tick) {
        recordCombatEvent(event, tick);
    };
    const LifecycleEventSink lifecycleEvents = [this](const LifecycleEvent& event, SimulationTick tick) {
        recordLifecycleEvent(event, tick);
    };
    auto assignments = WorkerAssignmentSystem::step();
    if (!assignments) return failureFrom<std::size_t>(assignments.status());
    std::size_t processed = std::move(assignments).takeValue();

    auto workforce = WorkforceAssignmentSystem::step();
    if (!workforce) return failureFrom<std::size_t>(workforce.status());
    processed += std::move(workforce).takeValue();

    auto ai = AISystem::step(simulationStep, aiProductionRequest_);
    if (!ai) return failureFrom<std::size_t>(ai.status());
    processed += std::move(ai).takeValue();

    auto tactics = TacticsSystem::step(damage_, simulationStep);
    if (!tactics) return failureFrom<std::size_t>(tactics.status());
    processed += std::move(tactics).takeValue();

    auto command = CommandNetworkSystem::step();
    if (!command) return failureFrom<std::size_t>(command.status());
    processed += std::move(command).takeValue();

    auto commandState = CommandStateSystem::step();
    if (!commandState) return failureFrom<std::size_t>(commandState.status());
    processed += std::move(commandState).takeValue();

    auto patrol = PatrolSystem::step();
    if (!patrol) return failureFrom<std::size_t>(patrol.status());
    processed += std::move(patrol).takeValue();

    if (pathfinder_ != nullptr) {
        auto navigation = NavigationSystem::step(*pathfinder_, navigationGrid_, navigationEvent_);
        if (!navigation) return failureFrom<std::size_t>(navigation.status());
        processed += std::move(navigation).takeValue();
        auto traffic = TrafficReservationSystem::step(*pathfinder_, navigationGrid_);
        if (!traffic) return failureFrom<std::size_t>(traffic.status());
        processed += std::move(traffic).takeValue();
    }

    auto convoy = SupplyConvoySystem::step();
    if (!convoy) return failureFrom<std::size_t>(convoy.status());
    processed += std::move(convoy).takeValue();

    if (fogProvider_) {
        auto fog = FogOfWarSystem::step(simulationStep, navigationGrid_, fogState_, fogProvider_);
        if (!fog) return failureFrom<std::size_t>(fog.status());
        processed += std::move(fog).takeValue();
    }

    auto motion = MotionSystem::step(simulationStep);
    if (!motion) return failureFrom<std::size_t>(motion.status());
    processed += std::move(motion).takeValue();

    if (crowd_ != nullptr) {
        auto crowdMotion = CrowdMotionSystem::step(simulationStep, *crowd_);
        if (!crowdMotion) return failureFrom<std::size_t>(crowdMotion.status());
        processed += std::move(crowdMotion).takeValue();
    }

    auto movementOrders = MovementOrderSystem::step();
    if (!movementOrders) return failureFrom<std::size_t>(movementOrders.status());
    processed += std::move(movementOrders).takeValue();

    auto containment = ContainmentSystem::step();
    if (!containment) return failureFrom<std::size_t>(containment.status());
    processed += std::move(containment).takeValue();

    auto morale = MoraleSystem::step(simulationStep, lifecycleEvents);
    if (!morale) return failureFrom<std::size_t>(morale.status());
    processed += std::move(morale).takeValue();

    auto shields = ShieldSystem::step(simulationStep, lifecycleEvents);
    if (!shields) return failureFrom<std::size_t>(shields.status());
    processed += std::move(shields).takeValue();

    auto supply = SupplySystem::step(simulationStep, ammoProductionPurchase_, pathfinder_, navigationGrid_,
        [this](const LifecycleEvent& event, SimulationTick tick) { recordLifecycleEvent(event, tick); });
    if (!supply) return failureFrom<std::size_t>(supply.status());
    processed += std::move(supply).takeValue();

    auto artillery = ArtillerySystem::step(simulationStep);
    if (!artillery) return failureFrom<std::size_t>(artillery.status());
    processed += std::move(artillery).takeValue();

    auto fireSupport = FireSupportSystem::step(simulationStep);
    if (!fireSupport) return failureFrom<std::size_t>(fireSupport.status());
    processed += std::move(fireSupport).takeValue();

    if (resourceCredit_) {
        auto mining = MiningSystem::step(simulationStep, resourceCredit_);
        if (!mining) return failureFrom<std::size_t>(mining.status());
        processed += std::move(mining).takeValue();
    }

    auto construction = ConstructionSystem::step(simulationStep, lifecycleEvents);
    if (!construction) return failureFrom<std::size_t>(construction.status());
    processed += std::move(construction).takeValue();

    auto infrastructure = InfrastructureSystem::step(simulationStep, passiveIncomeCredit_);
    if (!infrastructure) return failureFrom<std::size_t>(infrastructure.status());
    processed += std::move(infrastructure).takeValue();

    if (repairDebit_) {
        auto repair = RepairSystem::step(simulationStep, repairDebit_);
        if (!repair) return failureFrom<std::size_t>(repair.status());
        processed += std::move(repair).takeValue();
    }

    auto capture = CaptureSystem::step(simulationStep, lifecycleEvents);
    if (!capture) return failureFrom<std::size_t>(capture.status());
    processed += std::move(capture).takeValue();

    if (damage_ != nullptr) {
        auto abilities = AbilitySystem::step(simulationStep, *damage_, damageEvents, lifecycleEvents);
        if (!abilities) return failureFrom<std::size_t>(abilities.status());
        processed += std::move(abilities).takeValue();
        auto projectileImpacts = projectiles_.step(
            simulationStep, *damage_, projectileCollisionQuery_, damageEvents);
        if (!projectileImpacts) return failureFrom<std::size_t>(projectileImpacts.status());
        processed += std::move(projectileImpacts).takeValue();
    }
    if (sensing_ != nullptr && damage_ != nullptr) {
        auto combat = CombatFireSystem::step(simulationStep, combatState_, *sensing_, *damage_, &projectiles_,
                                             fireLineQuery_, pathfinder_, navigationGrid_, combatHeightQuery_,
                                             damageEvents, fireEvents);
        if (!combat) return failureFrom<std::size_t>(combat.status());
        processed += std::move(combat).takeValue();
    }

    auto actions = OrderActionSystem::step(simulationStep, executor);
    if (!actions) return failureFrom<std::size_t>(actions.status());
    processed += std::move(actions).takeValue();

    auto reinforcementPolicy = ReinforcementProductionPolicySystem::step(simulationStep, reinforcementCancel_);
    if (!reinforcementPolicy) return failureFrom<std::size_t>(reinforcementPolicy.status());
    processed += std::move(reinforcementPolicy).takeValue();

    auto production = BuildingProductionSystem::step(
        simulationStep, productionSpawn_, productionSpawnPosition_, lifecycleEvents);
    if (!production) return failureFrom<std::size_t>(production.status());
    processed += std::move(production).takeValue();
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr || unit->attributes()->values.initialized()) continue;
        auto initialized = RTSUnitAttributeAdapter::ensure(*unit);
        if (!initialized) return failureFrom<std::size_t>(initialized.status());
    }

    auto reinforcements = ReinforcementSystem::step();
    if (!reinforcements) return failureFrom<std::size_t>(reinforcements.status());
    processed += std::move(reinforcements).takeValue();
    if (definitions_ != nullptr) {
        auto technology = TechnologySystem::step(*definitions_);
        if (!technology) return failureFrom<std::size_t>(technology.status());
        processed += std::move(technology).takeValue();
    }

    auto effects = EffectSystem::step(simulationStep, lifecycleEvents);
    if (!effects) return failureFrom<std::size_t>(effects.status());
    processed += std::move(effects).takeValue();
    for (const auto& handle : matches_) {
        auto* match = dynamic_cast<Match*>(ecs::try_get(handle));
        if (match == nullptr) continue;
        auto outcome = MatchSystem::step(*match, matchResourceQuery_);
        if (!outcome) return failureFrom<std::size_t>(outcome.status());
        processed += std::move(outcome).takeValue();
    }
    auto cleanup = cleanupDestroyed();
    if (!cleanup) return failureFrom<std::size_t>(cleanup.status());
    processed += std::move(cleanup).takeValue();
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> RTS::stepScript(double seconds) {
    auto duration = Duration::fromSeconds(seconds);
    if (!duration || seconds <= 0.0) {
        if (!duration) return Result<std::size_t>::failure(duration.status());
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS script step duration must be positive", "seconds");
    }
    if (!scriptRuntime_) scriptRuntime_ = std::make_unique<ScriptRuntime>();
    if (scriptRuntime_->nextTick == std::numeric_limits<std::uint64_t>::max())
        return failure<std::size_t>(DiagnosticCode::Conflict, "RTS script simulation tick overflow", "tick");
    const SimulationTick tick{scriptRuntime_->nextTick++};
    frameDamageEvents_.clear();
    frameCombatEvents_.clear();
    frameLifecycleEvents_.clear();
    frameEventSequence_ = 0;
    preserveFrameEventsOnNextStep_ = true;
    auto commands = scriptRuntime_->commandLog.apply(tick, *this);
    if (!commands) {
        preserveFrameEventsOnNextStep_ = false;
        return failureFrom<std::size_t>(commands.status());
    }
    const SimulationStep simulationStep{tick, std::move(duration).takeValue()};
    auto stepped = step(simulationStep, scriptRuntime_->adapter);
    if (!stepped) return stepped;
    std::erase_if(scriptRuntime_->paidProduction, [this](const auto& record) {
        auto* producer = findBuilding(record.producer);
        if (producer == nullptr) return true;
        auto task = producer->production()->values.find(record.taskId);
        return !task || task->get().state == production::TaskState::Completed ||
               task->get().state == production::TaskState::Cancelled ||
               task->get().state == production::TaskState::Failed;
    });
    return Result<std::size_t>::success(commands.value() + stepped.value(), Status::success(StatusCode::Applied));
}

Result<void> RTS::configureScriptWorld(int width, int height, float cellSize, float originX, float originY) {
    if (width <= 0 || height <= 0 || !std::isfinite(cellSize) || cellSize <= 0.0f ||
        !std::isfinite(originX) || !std::isfinite(originY))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS script world requires positive dimensions and finite grid geometry", "grid");
    if (!scriptRuntime_) scriptRuntime_ = std::make_unique<ScriptRuntime>();
    scriptRuntime_->pathfinder.setSize(width, height);
    scriptRuntime_->crowd.resizeField(width, height, cellSize, originX, originY);
    scriptRuntime_->width = width;
    scriptRuntime_->height = height;
    scriptRuntime_->cellSize = cellSize;
    scriptRuntime_->originX = originX;
    scriptRuntime_->originY = originY;
    scriptRuntime_->terrainElevations.assign(static_cast<std::size_t>(width * height), 0.0f);
    scriptRuntime_->commandLog.clear();
    scriptRuntime_->nextTick = 1;
    scriptRuntime_->aiProductionSequence = 1;
    scriptRuntime_->configured = true;
    setDefinitionRegistry(&scriptRuntime_->definitions);
    setNavigationProvider(&scriptRuntime_->pathfinder, {cellSize, originX, originY});
    setCrowdProvider(&scriptRuntime_->crowd);
    setCombatProviders(&scriptRuntime_->sensing, &scriptRuntime_->damage);
    const auto firstBlockedPoint = [this](WorldPosition from, WorldPosition to) -> std::optional<WorldPosition> {
        if (!scriptRuntime_ || !scriptRuntime_->configured) return std::nullopt;
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float distance = std::hypot(dx, dy);
        const float sampleLength = std::max(scriptRuntime_->cellSize * 0.25f, 0.001f);
        const int samples = std::max(1, static_cast<int>(std::ceil(distance / sampleLength)));
        int previousX = static_cast<int>(std::floor((from.x - scriptRuntime_->originX) /
                                                     scriptRuntime_->cellSize));
        int previousY = static_cast<int>(std::floor((from.y - scriptRuntime_->originY) /
                                                     scriptRuntime_->cellSize));
        for (int sample = 1; sample <= samples; ++sample) {
            const float t = static_cast<float>(sample) / static_cast<float>(samples);
            const WorldPosition point{from.x + dx * t, from.y + dy * t};
            const int cellX = static_cast<int>(std::floor((point.x - scriptRuntime_->originX) /
                                                          scriptRuntime_->cellSize));
            const int cellY = static_cast<int>(std::floor((point.y - scriptRuntime_->originY) /
                                                          scriptRuntime_->cellSize));
            if (cellX == previousX && cellY == previousY) continue;
            previousX = cellX;
            previousY = cellY;
            if (cellX < 0 || cellY < 0 || cellX >= scriptRuntime_->width || cellY >= scriptRuntime_->height ||
                !scriptRuntime_->pathfinder.isWalkable(cellX, cellY))
                return point;
        }
        return std::nullopt;
    };
    const auto terrainAt = [this](WorldPosition point) {
        if (!scriptRuntime_ || scriptRuntime_->terrainElevations.empty()) return 0.0f;
        const int x = static_cast<int>(std::floor((point.x - scriptRuntime_->originX) /
                                                  scriptRuntime_->cellSize));
        const int y = static_cast<int>(std::floor((point.y - scriptRuntime_->originY) /
                                                  scriptRuntime_->cellSize));
        if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height) return 0.0f;
        return scriptRuntime_->terrainElevations[static_cast<std::size_t>(y * scriptRuntime_->width + x)];
    };
    const auto relativeHeight = [](ecs::EntityHandle handle, bool firing) {
        if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle)))
            return firing ? unit->combat()->firingHeight : unit->combat()->targetHeight;
        if (auto* building = dynamic_cast<Building*>(ecs::try_get(handle)))
            return firing ? building->combat()->firingHeight : building->combat()->targetHeight;
        return firing ? 1.0f : 0.0f;
    };
    const auto terrainBlocks = [this, terrainAt](WorldPosition from, WorldPosition to,
                                                  float fromHeight, float toHeight) {
        const float distance = std::hypot(to.x - from.x, to.y - from.y);
        const float sampleLength = std::max(scriptRuntime_->cellSize * 0.25f, 0.001f);
        const int samples = std::max(1, static_cast<int>(std::ceil(distance / sampleLength)));
        for (int sample = 1; sample < samples; ++sample) {
            const float t = static_cast<float>(sample) / static_cast<float>(samples);
            const WorldPosition point{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
            if (terrainAt(point) >= fromHeight + (toHeight - fromHeight) * t) return std::optional{point};
        }
        return std::optional<WorldPosition>{};
    };
    setFireLineQuery([firstBlockedPoint, terrainAt, relativeHeight, terrainBlocks](
                         WorldPosition from, WorldPosition to, ecs::EntityHandle source,
                         ecs::EntityHandle target, const weapon::WeaponDefinition&) -> Result<bool> {
        const float sourceHeight = terrainAt(from) + relativeHeight(source, true);
        const float targetHeight = terrainAt(to) + relativeHeight(target, false);
        const bool blocked = firstBlockedPoint(from, to).has_value() ||
                             terrainBlocks(from, to, sourceHeight, targetHeight).has_value();
        return Result<bool>::success(!blocked,
                                     Status::success(StatusCode::Applied));
    });
    setCombatHeightQuery([terrainAt, relativeHeight](WorldPosition from, WorldPosition to,
                                                      ecs::EntityHandle source, ecs::EntityHandle target) {
        return CombatHeightProfile{terrainAt(from) + relativeHeight(source, true),
                                   terrainAt(to) + relativeHeight(target, false)};
    });
    setProjectileCollisionQuery(
        [firstBlockedPoint, terrainBlocks](WorldPosition from, float fromHeight,
                                           WorldPosition to, float toHeight, SubjectRef,
                                           ecs::EntityHandle) -> Result<std::optional<ProjectileCollision>> {
            const auto blocked = firstBlockedPoint(from, to);
            const auto terrainImpact = terrainBlocks(from, to, fromHeight, toHeight);
            if (!blocked && !terrainImpact)
                return Result<std::optional<ProjectileCollision>>::success(
                    std::nullopt, Status::success(StatusCode::NoOp));
            const WorldPosition impact = blocked ? *blocked : *terrainImpact;
            return Result<std::optional<ProjectileCollision>>::success(
                ProjectileCollision{impact, {}}, Status::success(StatusCode::Applied));
        });
    setFogProvider([this](Faction& faction) -> map::Fov* {
        if (!scriptRuntime_) return nullptr;
        const auto found = scriptRuntime_->fovs.find(faction.identity()->subject.format());
        return found == scriptRuntime_->fovs.end() ? nullptr : found->second.get();
    });
    setProductionSpawn([this](Building& producer, const production::ProductionTask& task) -> Result<Unit*> {
        if (!scriptRuntime_)
            return failure<Unit*>(DiagnosticCode::Conflict, "RTS script runtime is unavailable", "production");
        const auto pending = scriptRuntime_->pendingProductionSubjects.find(task.id);
        if (pending == scriptRuntime_->pendingProductionSubjects.end())
            return failure<Unit*>(DiagnosticCode::NotFound,
                                  "RTS produced unit has no reserved stable subject", "production.task");
        const auto definition = LogicalId::parse("unit:" + task.product);
        if (!definition)
            return failure<Unit*>(DiagnosticCode::InvalidArgument,
                                  "RTS produced unit has an invalid logical definition", "production.product");
        auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
        if (faction == nullptr)
            return failure<Unit*>(DiagnosticCode::StaleHandle,
                                  "RTS producer faction link is stale", "production.faction");
        scriptRuntime_->spawningProduction = true;
        auto created = newFactionUnit(*faction, pending->second, *definition);
        scriptRuntime_->spawningProduction = false;
        if (created) scriptRuntime_->pendingProductionSubjects.erase(pending);
        return created;
    });
    setAIProductionRequest([this](Faction& faction, Building& producer,
                                  const LogicalId& definition) -> Result<void> {
        if (!scriptRuntime_ || !scriptRuntime_->configured)
            return failure<void>(DiagnosticCode::Conflict,
                "RTS script AI production requires a configured world", "scriptRuntime");
        if (scriptRuntime_->aiProductionSequence == std::numeric_limits<std::uint64_t>::max())
            return failure<void>(DiagnosticCode::Conflict,
                "RTS script AI production identity sequence overflow", "ai.sequence");
        SubjectRef generated;
        do {
            generated = deterministicSubject(
                faction.identity()->subject.format() + ":" + producer.identity()->subject.format() +
                    ":" + definition.format(),
                scriptRuntime_->aiProductionSequence++);
        } while (ownsSubject(generated) &&
                 scriptRuntime_->aiProductionSequence != std::numeric_limits<std::uint64_t>::max());
        if (ownsSubject(generated))
            return failure<void>(DiagnosticCode::Conflict,
                "RTS script AI could not allocate a stable production subject", "ai.sequence");
        auto queued = queueScriptUnit(producer, generated, definition);
        if (!queued) return Result<void>::failure(queued.status());
        return Result<void>::success(Status::success(StatusCode::Applied));
    });

    for (const auto& handle : factions_) {
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (faction == nullptr) continue;
        const std::string key = faction->identity()->subject.format();
        if (!scriptRuntime_->economies.contains(key))
            scriptRuntime_->economies.emplace(key, std::make_unique<ScriptRuntime::EconomySlot>());
        auto& fov = scriptRuntime_->fovs[key];
        if (!fov) fov = std::make_unique<map::Fov>(width, height);
        else fov->setSize(width, height);
        fov->setMode("heightmap");
        fov->setEyeOffset(1.0f);
        fov->setCliffBlock(0.0f);
        auto link = EconomyLink::bind("rts/script/economy/" + key);
        if (!link) return Result<void>::failure(link.status());
        faction->economy()->link = std::move(link).takeValue();
    }
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr) continue;
        const std::string key = unit->identity()->subject.format();
        auto crowdLink = CrowdLink::bind(key);
        auto sensingLink = SensingLink::bind(key);
        if (!crowdLink) return Result<void>::failure(crowdLink.status());
        if (!sensingLink) return Result<void>::failure(sensingLink.status());
        unit->crowd()->link = std::move(crowdLink).takeValue();
        unit->sensing()->link = std::move(sensingLink).takeValue();
    }

    const auto accountForFaction = [this](Faction* faction) -> resource::IResourceAccount* {
        if (faction == nullptr || !scriptRuntime_) return nullptr;
        const auto found = scriptRuntime_->economies.find(faction->identity()->subject.format());
        return found == scriptRuntime_->economies.end() ? nullptr : &found->second->account;
    };
    setResourceCredit([accountForFaction](Unit& unit, const resource::CostSpec& cost) -> Result<resource::Receipt> {
        auto* account = accountForFaction(dynamic_cast<Faction*>(unit.faction()->link.resolve()));
        if (account == nullptr)
            return failure<resource::Receipt>(DiagnosticCode::NotFound,
                                              "RTS script faction economy was not found", "unit.faction");
        return account->credit(cost);
    });
    setRepairDebit([accountForFaction](Unit& unit, Building&, const resource::CostSpec& cost)
                       -> Result<resource::Receipt> {
        auto* account = accountForFaction(dynamic_cast<Faction*>(unit.faction()->link.resolve()));
        if (account == nullptr)
            return failure<resource::Receipt>(DiagnosticCode::NotFound,
                                              "RTS script faction economy was not found", "unit.faction");
        return account->debit(cost);
    });
    setPassiveIncomeCredit([accountForFaction](Building& building, const resource::CostSpec& cost)
                               -> Result<resource::Receipt> {
        auto* account = accountForFaction(dynamic_cast<Faction*>(building.faction()->link.resolve()));
        if (account == nullptr)
            return failure<resource::Receipt>(DiagnosticCode::NotFound,
                                              "RTS script faction economy was not found", "building.faction");
        return account->credit(cost);
    });
    setMatchResourceQuery([this](Faction& faction, std::string_view resource) -> Result<double> {
        auto balance = scriptResource(faction, resource);
        if (!balance) return Result<double>::failure(balance.status());
        return Result<double>::success(static_cast<double>(balance.value()),
                                       Status::success(StatusCode::Applied));
    });
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setScriptNavigationBlocked(int x, int y, bool blocked) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict, "RTS script world is not configured", "grid");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height)
        return failure<void>(DiagnosticCode::InvalidArgument, "RTS script navigation cell is outside the grid",
                             "cell");
    scriptRuntime_->pathfinder.setBlocked(x, y, blocked);
    scriptRuntime_->crowd.setBlocked(x, y, blocked);
    for (auto& [key, fov] : scriptRuntime_->fovs) {
        (void)key;
        if (fov) fov->setOpaque(x, y, blocked);
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::rebindScriptRootProviders() {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict, "RTS script world is not configured", "scriptWorld");
    for (const auto& handle : factions_) {
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(handle));
        if (faction == nullptr) continue;
        const std::string key = faction->identity()->subject.format();
        if (!scriptRuntime_->economies.contains(key))
            scriptRuntime_->economies.emplace(key, std::make_unique<ScriptRuntime::EconomySlot>());
        if (!scriptRuntime_->fovs.contains(key) || !scriptRuntime_->fovs.at(key))
            scriptRuntime_->fovs[key] = std::make_unique<map::Fov>(scriptRuntime_->width, scriptRuntime_->height);
        scriptRuntime_->fovs[key]->setMode("heightmap");
        scriptRuntime_->fovs[key]->setEyeOffset(1.0f);
        scriptRuntime_->fovs[key]->setCliffBlock(0.0f);
        auto economy = EconomyLink::bind("rts/script/economy/" + key);
        if (!economy) return Result<void>::failure(economy.status());
        faction->economy()->link = std::move(economy).takeValue();
    }
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr) continue;
        const std::string key = unit->identity()->subject.format();
        auto crowdLink = CrowdLink::bind(key);
        auto sensingLink = SensingLink::bind(key);
        if (!crowdLink) return Result<void>::failure(crowdLink.status());
        if (!sensingLink) return Result<void>::failure(sensingLink.status());
        unit->crowd()->link = std::move(crowdLink).takeValue();
        unit->sensing()->link = std::move(sensingLink).takeValue();
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setScriptNavigationCost(int x, int y, float cost) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict, "RTS script world is not configured", "grid");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height ||
        !std::isfinite(cost) || cost <= 0.0f)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS script navigation cost requires an in-grid cell and positive finite cost", "cell");
    scriptRuntime_->pathfinder.setCellCost(x, y, cost);
    scriptRuntime_->crowd.setCellCost(x, y, cost);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setScriptTerrainElevation(int x, int y, float elevation) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict, "RTS script world is not configured", "terrain");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height ||
        !std::isfinite(elevation))
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS terrain elevation requires an in-grid cell and finite value", "terrain.cell");
    scriptRuntime_->terrainElevations[static_cast<std::size_t>(y * scriptRuntime_->width + x)] = elevation;
    for (auto& [faction, fov] : scriptRuntime_->fovs) {
        (void)faction;
        if (fov) fov->setElevation(x, y, elevation);
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<float> RTS::scriptTerrainElevation(int x, int y) const {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<float>(DiagnosticCode::Conflict, "RTS script world is not configured", "terrain");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height)
        return failure<float>(DiagnosticCode::InvalidArgument, "RTS terrain cell is outside the grid", "terrain.cell");
    return Result<float>::success(
        scriptRuntime_->terrainElevations[static_cast<std::size_t>(y * scriptRuntime_->width + x)],
        Status::success(StatusCode::Applied));
}

Result<void> RTS::addScriptResource(Faction& faction, std::string resource, std::int64_t amount) {
    if (!owns(factions_, faction))
        return failure<void>(DiagnosticCode::StaleHandle, "RTS Faction does not belong to this facade", "faction");
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict, "RTS script world is not configured", "economy");
    const auto found = scriptRuntime_->economies.find(faction.identity()->subject.format());
    if (found == scriptRuntime_->economies.end())
        return failure<void>(DiagnosticCode::NotFound, "RTS script faction economy was not found", "faction");
    auto cost = resource::CostSpec::single(std::move(resource), amount);
    if (!cost) return Result<void>::failure(cost.status());
    auto credited = found->second->account.credit(std::move(cost).takeValue());
    if (!credited) return Result<void>::failure(credited.status());
    std::move(credited).takeValue();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::int64_t> RTS::scriptResource(Faction& faction, std::string_view resource) const {
    if (!owns(factions_, faction))
        return failure<std::int64_t>(DiagnosticCode::StaleHandle,
                                     "RTS Faction does not belong to this facade", "faction");
    if (!scriptRuntime_ || !scriptRuntime_->configured || resource.empty())
        return failure<std::int64_t>(DiagnosticCode::InvalidArgument,
                                     "RTS script resource query requires a configured world and resource", "resource");
    const auto found = scriptRuntime_->economies.find(faction.identity()->subject.format());
    if (found == scriptRuntime_->economies.end())
        return failure<std::int64_t>(DiagnosticCode::NotFound, "RTS script faction economy was not found", "faction");
    return Result<std::int64_t>::success(found->second->ledger.get(std::string(resource)));
}

Result<void> RTS::configureScriptAI(Faction& faction, LogicalId workerDefinition,
    LogicalId armyDefinition, LogicalId targetBuildingDefinition, int desiredWorkers,
    int attackThreshold, float thinkInterval, float formationSpacing, bool enabled) {
    if (!owns(factions_, faction))
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS AI faction does not belong to this facade", "faction");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr)
        return failure<void>(DiagnosticCode::Conflict,
            "RTS AI requires a configured script world and content", "scriptRuntime");
    if (!workerDefinition.isValid() || !armyDefinition.isValid() ||
        !targetBuildingDefinition.isValid() || desiredWorkers < 0 || attackThreshold <= 0 ||
        !std::isfinite(thinkInterval) || thinkInterval <= 0.0f ||
        !std::isfinite(formationSpacing) || formationSpacing <= 0.0f ||
        !definitions_->resolve("unit", std::string(workerDefinition.name())) ||
        !definitions_->resolve("unit", std::string(armyDefinition.name())) ||
        !definitions_->resolve("building", std::string(targetBuildingDefinition.name())))
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS AI policy requires known definitions and positive finite policy values", "strategy");
    auto strategy = faction.strategy();
    strategy->workerDefinition = std::move(workerDefinition);
    strategy->armyDefinition = std::move(armyDefinition);
    strategy->targetBuildingDefinition = std::move(targetBuildingDefinition);
    strategy->desiredWorkers = desiredWorkers;
    strategy->attackThreshold = attackThreshold;
    strategy->thinkInterval = thinkInterval;
    strategy->thinkAccumulator = 0.0f;
    strategy->formationSpacing = formationSpacing;
    strategy->enabled = enabled;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<ContentImportReceipt> RTS::loadScriptContent(std::string_view json) {
    if (!scriptRuntime_) scriptRuntime_ = std::make_unique<ScriptRuntime>();
    setDefinitionRegistry(&scriptRuntime_->definitions);
    return loadContent(scriptRuntime_->definitions, json);
}

Result<RTSBuildReceipt> RTS::queueScriptUnit(Building& producer, SubjectRef unitSubject,
                                              LogicalId unitDefinition, int priority) {
    if (!owns(buildings_, producer))
        return failure<RTSBuildReceipt>(DiagnosticCode::StaleHandle,
                                        "RTS producer does not belong to this facade", "producer");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr)
        return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                        "RTS script world and content must be configured", "scriptRuntime");
    if (!unitSubject.isValid() || ownsSubject(unitSubject))
        return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                        "RTS produced unit subject is invalid or already owned", "unitSubject");
    if (!unitDefinition.isValid())
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS production requires a unit definition", "unitDefinition");
    auto resolved = definitions_->resolve("unit", std::string(unitDefinition.name()));
    if (!resolved) return Result<RTSBuildReceipt>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<RTSBuildReceipt>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS unit definition must be an object", "unitDefinition");
    const auto resourceIt = object->find("costResource");
    const auto costIt = object->find("cost");
    const auto timeIt = object->find("buildTime");
    const auto producerIt = object->find("producer");
    if (resourceIt == object->end() || costIt == object->end() || timeIt == object->end())
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS unit definition lacks production cost or duration", "unitDefinition");
    const auto* resourceName = resourceIt->second.getIf<std::string>();
    const auto number = [](const Value& value) -> std::optional<double> {
        if (const auto* integer = value.getIf<std::int64_t>()) return static_cast<double>(*integer);
        if (const auto* real = value.getIf<double>()) return *real;
        return std::nullopt;
    };
    const auto costNumber = number(costIt->second);
    const auto timeNumber = number(timeIt->second);
    if (resourceName == nullptr || resourceName->empty() || !costNumber || !timeNumber ||
        !std::isfinite(*costNumber) || *costNumber < 0.0 || std::floor(*costNumber) != *costNumber ||
        !std::isfinite(*timeNumber) || *timeNumber <= 0.0)
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS unit production values are invalid", "unitDefinition");
    if (producerIt != object->end()) {
        const auto* requiredProducer = producerIt->second.getIf<std::string>();
        if (requiredProducer == nullptr || *requiredProducer != producer.definition()->id.name())
            return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                            "RTS building cannot produce this unit definition", "producer");
    }
    auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
    if (faction == nullptr)
        return failure<RTSBuildReceipt>(DiagnosticCode::StaleHandle,
                                        "RTS producer faction link is stale", "producer.faction");
    const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<RTSBuildReceipt>(DiagnosticCode::NotFound,
                                        "RTS producer economy was not found", "producer.faction");
    auto cost = resource::CostSpec::single(*resourceName, static_cast<std::int64_t>(*costNumber));
    if (!cost) return Result<RTSBuildReceipt>::failure(cost.status());
    const resource::CostSpec paidCost = cost.value();
    auto duration = Duration::fromSeconds(*timeNumber);
    if (!duration) return Result<RTSBuildReceipt>::failure(duration.status());
    auto receipt = build(producer, scriptRuntime_->actions, economy->second->account,
                         std::move(cost).takeValue(), std::string(unitDefinition.name()),
                         std::move(duration).takeValue(), "unit", priority,
                         "rts.script.production." + unitSubject.format());
    if (!receipt) return receipt;
    scriptRuntime_->pendingProductionSubjects.emplace(receipt.value().productionTaskId, unitSubject);
    scriptRuntime_->paidProduction.push_back({producer.identity()->subject, unitSubject, "unit",
        std::string(unitDefinition.name()), receipt.value().productionTaskId, receipt.value().orderId, paidCost});
    return receipt;
}

Result<ReinforcementRequestReceipt> RTS::queueScriptReinforcement(
    Building& producer, SubjectRef unitSubject, LogicalId preferredDefinition, int priority) {
    if (!owns(buildings_, producer))
        return failure<ReinforcementRequestReceipt>(DiagnosticCode::StaleHandle,
            "RTS reinforcement producer does not belong to this facade", "producer");
    if (!unitSubject.isValid() || ownsSubject(unitSubject) || !preferredDefinition.isValid())
        return failure<ReinforcementRequestReceipt>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement requires a new stable subject and preferred unit definition", "reinforcement");
    return ReinforcementProductionPolicySystem::request(producer, std::string(preferredDefinition.name()),
        [&](Building& selectedProducer, std::string_view candidate) -> Result<std::string> {
            auto definition = LogicalId::parse(candidate);
            if (!definition)
                definition = LogicalId::parse("unit:" + std::string(candidate));
            if (!definition)
                return failure<std::string>(DiagnosticCode::InvalidArgument,
                    "RTS reinforcement fallback is not a valid unit definition", "reinforcement.fallback");
            auto queued = queueScriptUnit(selectedProducer, unitSubject, *definition, priority);
            if (!queued) return Result<std::string>::failure(queued.status());
            return Result<std::string>::success(queued.value().productionTaskId,
                Status::success(StatusCode::Applied));
        });
}

Result<Building*> RTS::startScriptConstruction(Faction& faction, SubjectRef buildingSubject,
                                                LogicalId buildingDefinition, WorldPosition position,
                                                Unit& builder) {
    if (!owns(factions_, faction) || !owns(units_, builder))
        return failure<Building*>(DiagnosticCode::StaleHandle,
                                  "RTS construction faction or builder does not belong to this facade",
                                  "construction");
    if (builder.faction()->link.resolve() != &faction || builder.worker()->buildRate <= 0.0f)
        return failure<Building*>(DiagnosticCode::PreconditionViolation,
                                  "RTS construction requires a same-faction builder", "builder");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr ||
        !buildingSubject.isValid() || ownsSubject(buildingSubject) || !buildingDefinition.isValid() ||
        !std::isfinite(position.x) || !std::isfinite(position.y))
        return failure<Building*>(DiagnosticCode::InvalidArgument,
                                  "RTS construction request is invalid or the world is not configured",
                                  "construction");
    auto resolved = definitions_->resolve("building", std::string(buildingDefinition.name()));
    if (!resolved) return Result<Building*>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<Building*>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<Building*>(DiagnosticCode::InvalidArgument,
                                  "RTS building definition must be an object", "buildingDefinition");
    const auto resourceIt = object->find("costResource");
    const auto costIt = object->find("cost");
    if (resourceIt == object->end() || costIt == object->end())
        return failure<Building*>(DiagnosticCode::InvalidArgument,
                                  "RTS building definition lacks a construction cost", "buildingDefinition");
    const auto* resourceName = resourceIt->second.getIf<std::string>();
    double costNumber = -1.0;
    if (const auto* integer = costIt->second.getIf<std::int64_t>()) costNumber = static_cast<double>(*integer);
    else if (const auto* real = costIt->second.getIf<double>()) costNumber = *real;
    if (resourceName == nullptr || resourceName->empty() || !std::isfinite(costNumber) || costNumber < 0.0 ||
        std::floor(costNumber) != costNumber)
        return failure<Building*>(DiagnosticCode::InvalidArgument,
                                  "RTS building construction cost is invalid", "buildingDefinition");
    const auto economy = scriptRuntime_->economies.find(faction.identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<Building*>(DiagnosticCode::NotFound,
                                  "RTS construction economy was not found", "faction");
    auto cost = resource::CostSpec::single(*resourceName, static_cast<std::int64_t>(costNumber));
    if (!cost) return Result<Building*>::failure(cost.status());
    auto previousOrders = builder.orders()->values.snapshotState();
    if (!previousOrders) return Result<Building*>::failure(previousOrders.status());
    auto paid = economy->second->account.debit(cost.value());
    if (!paid) return Result<Building*>::failure(paid.status());

    const std::size_t weaponCount = weapons_.size();
    auto created = newFactionBuilding(faction, buildingSubject, buildingDefinition);
    if (!created) {
        economy->second->account.credit(cost.value()).ignore("compensate failed construction root creation");
        return created;
    }
    Building* building = created.value();
    building->placement()->worldX = position.x;
    building->placement()->worldY = position.y;
    building->construction()->progress = 0.0f;
    building->construction()->builders = {ecs::handle_of(&builder)};
    CommandSpec move;
    move.kind = OrderKind::Move;
    move.target = position;
    auto ordered = builder.orders()->values.replace(move);
    if (ordered) {
        CommandSpec buildCommand;
        buildCommand.kind = OrderKind::Build;
        buildCommand.target = position;
        buildCommand.targetEntity = ecs::handle_of(building);
        ordered = builder.orders()->values.enqueue(buildCommand);
    }
    if (!ordered) {
        const Status status = ordered.status();
        builder.orders()->values.restoreState(previousOrders.value()).ignore("restore construction builder orders");
        faction.members()->buildings.pop_back();
        building->release();
        buildings_.pop_back();
        while (weapons_.size() > weaponCount) {
            if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(weapons_.back()))) weapon->release();
            weapons_.pop_back();
        }
        economy->second->account.credit(cost.value()).ignore("compensate failed construction order");
        return Result<Building*>::failure(status);
    }
    std::move(ordered).takeValue();
    scriptRuntime_->paidConstruction.push_back(
        {buildingSubject, faction.identity()->subject, cost.value()});
    return Result<Building*>::success(building, Status::success(StatusCode::Applied));
}

Result<resource::Receipt> RTS::cancelScriptConstruction(Building& building) {
    if (!owns(buildings_, building))
        return failure<resource::Receipt>(DiagnosticCode::StaleHandle,
            "RTS construction does not belong to this facade", "building");
    if (!scriptRuntime_ || !scriptRuntime_->configured || !building.integrity()->alive ||
        building.construction()->progress >= 1.0f)
        return failure<resource::Receipt>(DiagnosticCode::PreconditionViolation,
            "RTS construction cancellation requires a live unfinished script building", "building.construction");
    const auto payment = std::find_if(scriptRuntime_->paidConstruction.begin(),
        scriptRuntime_->paidConstruction.end(), [&](const auto& value) {
            return value.building == building.identity()->subject;
        });
    if (payment == scriptRuntime_->paidConstruction.end())
        return failure<resource::Receipt>(DiagnosticCode::NotFound,
            "RTS construction payment record was not found", "building.construction");
    auto* faction = findFaction(payment->faction);
    if (faction == nullptr)
        return failure<resource::Receipt>(DiagnosticCode::StaleHandle,
            "RTS construction faction is stale", "building.faction");
    const auto economy = scriptRuntime_->economies.find(payment->faction.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<resource::Receipt>(DiagnosticCode::NotFound,
            "RTS construction economy was not found", "building.faction");
    auto refund = scaledCost(payment->cost,
        1.0 - 0.5 * std::clamp(static_cast<double>(building.construction()->progress), 0.0, 1.0));
    if (!refund) return Result<resource::Receipt>::failure(refund.status());
    auto credited = economy->second->account.credit(refund.value());
    if (!credited) return Result<resource::Receipt>::failure(credited.status());

    for (const auto& builderHandle : building.construction()->builders) {
        auto* builder = dynamic_cast<Unit*>(ecs::try_get(builderHandle));
        if (builder != nullptr) builder->orders()->values.clear();
    }
    building.construction()->builders.clear();
    building.integrity()->alive = false;
    building.integrity()->state.health = 0.0;
    building.placement()->placed = false;
    const auto buildingHandle = ecs::handle_of(&building);
    std::erase_if(faction->members()->buildings,
        [&](const auto& value) { return sameHandle(value, buildingHandle); });
    const auto weaponHandle = building.weapon()->link.handle();
    if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(building.weapon()->link.resolve())) weapon->release();
    std::erase_if(weapons_, [&](const auto& value) { return sameHandle(value, weaponHandle); });
    std::erase_if(buildings_, [&](const auto& value) { return sameHandle(value, buildingHandle); });
    scriptRuntime_->paidConstruction.erase(payment);
    building.release();
    return credited;
}

Result<resource::Receipt> RTS::sellScriptBuilding(Building& building) {
    if (!owns(buildings_, building))
        return failure<resource::Receipt>(DiagnosticCode::StaleHandle,
            "RTS building does not belong to this facade", "building");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr ||
        !building.integrity()->alive || building.construction()->progress < 1.0f)
        return failure<resource::Receipt>(DiagnosticCode::PreconditionViolation,
            "RTS sale requires a live completed script building", "building.construction");
    auto* faction = dynamic_cast<Faction*>(building.faction()->link.resolve());
    if (faction == nullptr)
        return failure<resource::Receipt>(DiagnosticCode::StaleHandle,
            "RTS building faction link is stale", "building.faction");
    const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<resource::Receipt>(DiagnosticCode::NotFound,
            "RTS building economy was not found", "building.faction");
    auto definition = definitions_->resolve("building", std::string(building.definition()->id.name()));
    if (!definition) return Result<resource::Receipt>::failure(definition.status());
    auto parsed = Value::fromJson(definition.value().get().json);
    if (!parsed) return Result<resource::Receipt>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<resource::Receipt>(DiagnosticCode::InvalidArgument,
            "RTS building definition must be an object", "building.definition");
    const auto resourceIt = object->find("costResource");
    const auto costIt = object->find("cost");
    if (resourceIt == object->end() || costIt == object->end() ||
        resourceIt->second.getIf<std::string>() == nullptr)
        return failure<resource::Receipt>(DiagnosticCode::InvalidArgument,
            "RTS building definition lacks a sale cost", "building.definition");
    double costNumber = -1.0;
    if (const auto* integer = costIt->second.getIf<std::int64_t>()) costNumber = *integer;
    else if (const auto* real = costIt->second.getIf<double>()) costNumber = *real;
    double ratio = 0.5;
    if (const auto ratioIt = object->find("sellRefundRatio"); ratioIt != object->end()) {
        if (const auto* integer = ratioIt->second.getIf<std::int64_t>()) ratio = *integer;
        else if (const auto* real = ratioIt->second.getIf<double>()) ratio = *real;
        else ratio = -1.0;
    }
    if (!std::isfinite(costNumber) || costNumber <= 0.0 || std::floor(costNumber) != costNumber ||
        !std::isfinite(ratio) || ratio <= 0.0 || ratio > 1.0)
        return failure<resource::Receipt>(DiagnosticCode::InvalidArgument,
            "RTS building sale values are invalid", "building.definition");
    auto baseCost = resource::CostSpec::single(*resourceIt->second.getIf<std::string>(),
        static_cast<std::int64_t>(costNumber));
    if (!baseCost) return Result<resource::Receipt>::failure(baseCost.status());
    auto saleRefund = scaledCost(baseCost.value(), ratio);
    if (!saleRefund) return Result<resource::Receipt>::failure(saleRefund.status());

    auto rootBefore = snapshotState();
    if (!rootBefore) return Result<resource::Receipt>::failure(rootBefore.status());
    const auto ledgerBefore = economy->second->ledger.snapshot();
    const auto pendingBefore = scriptRuntime_->pendingProductionSubjects;
    const auto paymentsBefore = scriptRuntime_->paidProduction;
    const SubjectRef producerSubject = building.identity()->subject;
    const auto rollback = [&]() {
        economy->second->ledger.restore(ledgerBefore);
        scriptRuntime_->pendingProductionSubjects = pendingBefore;
        scriptRuntime_->paidProduction = paymentsBefore;
        (void)restoreState(rootBefore.value());
    };
    for (const auto& record : paymentsBefore) {
        if (record.producer != producerSubject) continue;
        auto task = building.production()->values.find(record.taskId);
        if (!task || task->get().state == production::TaskState::Completed ||
            task->get().state == production::TaskState::Cancelled ||
            task->get().state == production::TaskState::Failed) continue;
        auto cancelled = cancelProduction(building, economy->second->account, record.taskId,
                                           record.orderId, record.refund, "building sold");
        if (!cancelled) {
            rollback();
            return Result<resource::Receipt>::failure(cancelled.status());
        }
        scriptRuntime_->pendingProductionSubjects.erase(record.taskId);
        std::erase_if(scriptRuntime_->paidProduction,
            [&](const auto& value) { return value.taskId == record.taskId; });
    }
    auto credited = economy->second->account.credit(saleRefund.value());
    if (!credited) {
        rollback();
        return Result<resource::Receipt>::failure(credited.status());
    }
    auto evacuated = evacuateBuilding(building,
        {building.placement()->worldX + 2.0f, building.placement()->worldY});
    if (!evacuated) {
        rollback();
        return Result<resource::Receipt>::failure(evacuated.status());
    }

    const auto buildingHandle = ecs::handle_of(&building);
    for (const auto& factionHandle : factions_) {
        auto* owner = dynamic_cast<Faction*>(ecs::try_get(factionHandle));
        if (owner != nullptr)
            std::erase_if(owner->members()->buildings,
                [&](const auto& value) { return sameHandle(value, buildingHandle); });
    }
    const auto weaponHandle = building.weapon()->link.handle();
    if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(building.weapon()->link.resolve())) weapon->release();
    std::erase_if(weapons_, [&](const auto& value) { return sameHandle(value, weaponHandle); });
    std::erase_if(buildings_, [&](const auto& value) { return sameHandle(value, buildingHandle); });
    std::erase_if(scriptRuntime_->paidConstruction,
        [&](const auto& value) { return value.building == producerSubject; });
    building.release();
    return credited;
}

Result<RTSBuildReceipt> RTS::queueScriptResearch(Building& producer, std::string upgrade, int priority) {
    if (!owns(buildings_, producer))
        return failure<RTSBuildReceipt>(DiagnosticCode::StaleHandle,
                                        "RTS research producer does not belong to this facade", "producer");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr || upgrade.empty())
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS research requires a configured world and upgrade", "upgrade");
    auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
    if (faction == nullptr)
        return failure<RTSBuildReceipt>(DiagnosticCode::StaleHandle,
                                        "RTS research producer faction link is stale", "producer.faction");
    if (std::binary_search(faction->technology()->unlocked.begin(), faction->technology()->unlocked.end(), upgrade))
        return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                        "RTS upgrade is already unlocked", "upgrade");
    for (int index = 0; index < static_cast<int>(producer.production()->values.taskCount()); ++index) {
        auto task = producer.production()->values.taskAt(index);
        if (task && task->get().kind == "research" && task->get().product == upgrade &&
            task->get().state != production::TaskState::Cancelled &&
            task->get().state != production::TaskState::Failed)
            return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                            "RTS upgrade is already queued", "upgrade");
    }
    auto resolved = definitions_->resolve("upgrade", upgrade);
    if (!resolved) return Result<RTSBuildReceipt>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<RTSBuildReceipt>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS upgrade definition must be an object", "upgrade");
    const auto textField = [object](std::string_view name) -> std::string {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return {};
        const auto* value = found->second.getIf<std::string>();
        return value == nullptr ? std::string{} : *value;
    };
    const auto numberField = [object](std::string_view name) -> std::optional<double> {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return std::nullopt;
        if (const auto* integer = found->second.getIf<std::int64_t>()) return static_cast<double>(*integer);
        if (const auto* real = found->second.getIf<double>()) return *real;
        return std::nullopt;
    };
    const std::string requiredProducer = textField("producer");
    const std::string prerequisite = textField("prerequisiteUpgrade");
    const std::string resourceName = textField("costResource");
    const auto costNumber = numberField("cost");
    const auto researchTime = numberField("researchTime");
    if (requiredProducer != producer.definition()->id.name())
        return failure<RTSBuildReceipt>(DiagnosticCode::Conflict,
                                        "RTS building cannot research this upgrade", "producer");
    if (!prerequisite.empty() &&
        !std::binary_search(faction->technology()->unlocked.begin(), faction->technology()->unlocked.end(),
                            prerequisite))
        return failure<RTSBuildReceipt>(DiagnosticCode::PreconditionViolation,
                                        "RTS research prerequisite is not unlocked", "upgrade");
    if (resourceName.empty() || !costNumber || !researchTime || !std::isfinite(*costNumber) ||
        *costNumber < 0.0 || std::floor(*costNumber) != *costNumber || !std::isfinite(*researchTime) ||
        *researchTime <= 0.0)
        return failure<RTSBuildReceipt>(DiagnosticCode::InvalidArgument,
                                        "RTS upgrade cost or research duration is invalid", "upgrade");
    const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<RTSBuildReceipt>(DiagnosticCode::NotFound,
                                        "RTS research economy was not found", "producer.faction");
    auto cost = resource::CostSpec::single(resourceName, static_cast<std::int64_t>(*costNumber));
    auto duration = Duration::fromSeconds(*researchTime);
    if (!cost) return Result<RTSBuildReceipt>::failure(cost.status());
    if (!duration) return Result<RTSBuildReceipt>::failure(duration.status());
    const resource::CostSpec paidCost = cost.value();
    const std::string product = upgrade;
    auto receipt = build(producer, scriptRuntime_->actions, economy->second->account, std::move(cost).takeValue(),
                         std::move(upgrade), std::move(duration).takeValue(), "research", priority);
    if (receipt) scriptRuntime_->paidProduction.push_back({producer.identity()->subject, {}, "research", product,
        receipt.value().productionTaskId, receipt.value().orderId, paidCost});
    return receipt;
}

Result<RTSCancelProductionReceipt> RTS::cancelScriptProduction(Building& producer, int queueIndex) {
    if (!owns(buildings_, producer))
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::StaleHandle,
            "RTS producer does not belong to this facade", "producer");
    if (!scriptRuntime_ || !scriptRuntime_->configured || queueIndex < -1)
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::InvalidArgument,
            "RTS production cancellation requires a configured world and valid queue index", "queueIndex");
    std::vector<std::string> active;
    for (int index = 0; index < static_cast<int>(producer.production()->values.taskCount()); ++index) {
        auto task = producer.production()->values.taskAt(index);
        if (task && task->get().state != production::TaskState::Completed &&
            task->get().state != production::TaskState::Cancelled &&
            task->get().state != production::TaskState::Failed)
            active.push_back(task->get().id);
    }
    if (active.empty())
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::NotFound,
            "RTS producer has no cancellable production", "production");
    if (queueIndex < 0) queueIndex = static_cast<int>(active.size()) - 1;
    if (queueIndex >= static_cast<int>(active.size()))
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::NotFound,
            "RTS production queue index was not found", "queueIndex");
    const auto record = std::find_if(scriptRuntime_->paidProduction.begin(), scriptRuntime_->paidProduction.end(),
        [&](const auto& value) { return value.taskId == active[static_cast<std::size_t>(queueIndex)] &&
                                       value.producer == producer.identity()->subject; });
    if (record == scriptRuntime_->paidProduction.end())
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::NotFound,
            "RTS production payment record was not found", "production.task");
    auto* faction = dynamic_cast<Faction*>(producer.faction()->link.resolve());
    if (faction == nullptr)
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::StaleHandle,
            "RTS producer faction link is stale", "producer.faction");
    const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
    if (economy == scriptRuntime_->economies.end())
        return failure<RTSCancelProductionReceipt>(DiagnosticCode::NotFound,
            "RTS producer economy was not found", "producer.faction");
    const std::string taskId = record->taskId;
    auto cancelled = cancelProduction(producer, economy->second->account, record->taskId, record->orderId,
                                      record->refund, "script production cancelled");
    if (!cancelled) return cancelled;
    scriptRuntime_->pendingProductionSubjects.erase(taskId);
    scriptRuntime_->paidProduction.erase(record);
    return cancelled;
}

Result<void> RTS::setBuildingRally(
    Building& producer, CommandSpec command, bool groupedReinforcements) const {
    if (!owns(buildings_, producer))
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS rally producer does not belong to this facade", "producer");
    auto valid = command.validate();
    if (!valid) return valid;
    if (command.kind != OrderKind::Move && command.kind != OrderKind::AttackMove)
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS rally command must be Move or AttackMove", "command.kind");
    auto& rally = *producer.rally();
    rally = {};
    rally.enabled = true;
    rally.command = std::move(command);
    rally.combatGroup = groupedReinforcements
        ? stableRallyGroup(producer.identity()->subject) : 0;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::linkBuildingRally(Building& producer, Building& source) const {
    if (!owns(buildings_, producer) || !owns(buildings_, source) || &producer == &source)
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS rally link requires two distinct owned producers", "producer");
    if (producer.faction()->link.resolve() != source.faction()->link.resolve() ||
        !source.rally()->enabled || source.rally()->combatGroup == 0)
        return failure<void>(DiagnosticCode::PreconditionViolation,
            "RTS rally source must be a grouped friendly rally", "source.rally");
    auto linked = *source.rally();
    linked.transport = {};
    linked.minimumTransportLoad = 1;
    linked.transportActive = false;
    linked.productionSpawnBlocked = false;
    linked.blockedProductionTask.clear();
    linked.settledProductionTasks.clear();
    linked.reinforcements.clear();
    linked.reinforcementCapped = false;
    linked.reinforcementPolicyPausedTask.clear();
    linked.reinforcementCappedSeconds = 0.0f;
    *producer.rally() = std::move(linked);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::clearBuildingRally(Building& producer) const {
    if (!owns(buildings_, producer))
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS rally producer does not belong to this facade", "producer");
    *producer.rally() = {};
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementLimit(Building& producer, std::size_t maximum) const {
    if (!owns(buildings_, producer) || !producer.rally()->enabled || producer.rally()->combatGroup == 0)
        return failure<void>(DiagnosticCode::PreconditionViolation,
            "RTS reinforcement limit requires an owned grouped rally", "producer.rally");
    const auto group = producer.rally()->combatGroup;
    auto* faction = producer.faction()->link.resolve();
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr || building->faction()->link.resolve() != faction ||
            building->rally()->combatGroup != group) continue;
        building->rally()->reinforcementLimit = maximum;
        building->rally()->reinforcementCapped = false;
        building->rally()->reinforcementCappedSeconds = 0.0f;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementTypeLimit(
    Building& producer, std::string unitType, std::size_t maximum) const {
    if (!owns(buildings_, producer) || !producer.rally()->enabled || producer.rally()->combatGroup == 0 ||
        unitType.empty() || definitions_ == nullptr || !definitions_->resolve("unit", unitType))
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement type limit requires a grouped rally and known unit type", "unitType");
    const auto group = producer.rally()->combatGroup;
    auto* faction = producer.faction()->link.resolve();
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr || building->faction()->link.resolve() != faction ||
            building->rally()->combatGroup != group) continue;
        if (maximum == 0) building->rally()->reinforcementTypeLimits.erase(unitType);
        else building->rally()->reinforcementTypeLimits[unitType] = maximum;
        building->rally()->reinforcementCapped = false;
        building->rally()->reinforcementCappedSeconds = 0.0f;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementTypePriority(
    Building& producer, std::string unitType, int priority) const {
    if (!owns(buildings_, producer) || !producer.rally()->enabled || producer.rally()->combatGroup == 0 ||
        unitType.empty() || priority < 0 || definitions_ == nullptr || !definitions_->resolve("unit", unitType))
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement priority requires a grouped rally, known unit type, and non-negative priority",
            "priority");
    const auto group = producer.rally()->combatGroup;
    auto* faction = producer.faction()->link.resolve();
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr || building->faction()->link.resolve() != faction ||
            building->rally()->combatGroup != group) continue;
        if (priority == 0) building->rally()->reinforcementTypePriorities.erase(unitType);
        else building->rally()->reinforcementTypePriorities[unitType] = priority;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementFallback(
    Building& producer, std::string preferred, std::string fallback) const {
    if (!owns(buildings_, producer) || !producer.rally()->enabled || producer.rally()->combatGroup == 0 ||
        preferred.empty() || preferred == fallback || definitions_ == nullptr ||
        !definitions_->resolve("unit", preferred) || (!fallback.empty() && !definitions_->resolve("unit", fallback)))
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement fallback requires a grouped rally and known distinct unit types", "fallback");
    auto strategy = producer.rally()->reinforcementFallbacks;
    if (fallback.empty()) strategy.erase(preferred);
    else strategy[preferred] = fallback;
    for (const auto& [start, ignored] : strategy) {
        (void)ignored;
        std::set<std::string> visited;
        std::string current = start;
        while (true) {
            const auto next = strategy.find(current);
            if (next == strategy.end()) break;
            if (!visited.insert(current).second)
                return failure<void>(DiagnosticCode::Conflict,
                    "RTS reinforcement fallback chain contains a cycle", "fallback");
            current = next->second;
        }
    }
    const auto group = producer.rally()->combatGroup;
    auto* faction = producer.faction()->link.resolve();
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building != nullptr && building->faction()->link.resolve() == faction &&
            building->rally()->combatGroup == group)
            building->rally()->reinforcementFallbacks = strategy;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementAutoCancel(Building& producer, float seconds) const {
    if (!owns(buildings_, producer) || !producer.rally()->enabled || producer.rally()->combatGroup == 0 ||
        !std::isfinite(seconds) || seconds < 0.0f)
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement auto-cancel requires a grouped rally and non-negative delay", "seconds");
    const auto group = producer.rally()->combatGroup;
    auto* faction = producer.faction()->link.resolve();
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr || building->faction()->link.resolve() != faction ||
            building->rally()->combatGroup != group) continue;
        building->rally()->reinforcementAutoCancelDelay = seconds;
        building->rally()->reinforcementCappedSeconds = 0.0f;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::setReinforcementTransport(
    Building& producer, Unit* transport, std::size_t minimumLoad) const {
    if (!owns(buildings_, producer))
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS rally producer does not belong to this facade", "producer");
    if (transport == nullptr) {
        if (minimumLoad != 0)
            return failure<void>(DiagnosticCode::InvalidArgument,
                "RTS clearing a reinforcement transport requires zero minimum load", "minimumLoad");
        producer.rally()->transport = {};
        producer.rally()->minimumTransportLoad = 1;
        producer.rally()->transportActive = false;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (!owns(units_, *transport) || transport->faction()->link.resolve() != producer.faction()->link.resolve() ||
        transport->containment()->capacity == 0 || minimumLoad == 0 ||
        minimumLoad > transport->containment()->capacity || transport->containment()->container.isBound())
        return failure<void>(DiagnosticCode::PreconditionViolation,
            "RTS reinforcement transport must be an uncontained friendly carrier with a valid minimum load",
            "transport");
    const auto handle = ecs::handle_of(transport);
    for (const auto& buildingHandle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(buildingHandle));
        const auto assigned = building == nullptr ? ecs::EntityHandle{} : building->rally()->transport;
        const bool sameTransport = assigned.table == handle.table && assigned.type == handle.type &&
                                   assigned.id == handle.id && assigned.generation == handle.generation;
        if (building != nullptr && building != &producer && sameTransport)
            return failure<void>(DiagnosticCode::Conflict,
                "RTS reinforcement transport is already assigned to another producer", "transport");
    }
    producer.rally()->transport = handle;
    producer.rally()->minimumTransportLoad = minimumLoad;
    producer.rally()->transportActive = false;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::castScriptAbility(Unit& caster, std::string ability, SubjectRef target,
                                    WorldPosition point) {
    if (!owns(units_, caster))
        return failure<void>(DiagnosticCode::StaleHandle,
                             "RTS ability caster does not belong to this facade", "caster");
    if (!scriptRuntime_ || !scriptRuntime_->configured || definitions_ == nullptr || ability.empty())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS ability requires configured script content", "ability");
    auto resolved = definitions_->resolve("ability", ability);
    if (!resolved) return Result<void>::failure(resolved.status());
    auto parsed = Value::fromJson(resolved.value().get().json);
    if (!parsed) return Result<void>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (object == nullptr)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS ability definition must be an object", "ability");
    const auto textField = [object](std::string_view name, std::string fallback = {}) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return fallback;
        const auto* value = found->second.getIf<std::string>();
        return value == nullptr ? fallback : *value;
    };
    const auto numberField = [object](std::string_view name, double fallback) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return std::optional<double>{fallback};
        if (const auto* integer = found->second.getIf<std::int64_t>())
            return std::optional<double>{static_cast<double>(*integer)};
        if (const auto* real = found->second.getIf<double>()) return std::optional<double>{*real};
        return std::optional<double>{};
    };
    const auto boolField = [object](std::string_view name, bool fallback) {
        const auto found = object->find(std::string(name));
        if (found == object->end()) return std::optional<bool>{fallback};
        const auto* value = found->second.getIf<bool>();
        return value == nullptr ? std::optional<bool>{} : std::optional<bool>{*value};
    };
    AbilitySpec spec;
    spec.id = ability;
    const std::string casterDefinition = textField("casterUnit");
    if (!casterDefinition.empty()) {
        const auto id = LogicalId::fromParts("unit", casterDefinition);
        if (!id) return failure<void>(DiagnosticCode::InvalidArgument,
                                      "RTS ability caster definition is invalid", "casterUnit");
        spec.casterDefinition = *id;
    }
    const std::string targetType = textField("targetType", "enemy");
    if (targetType == "self") spec.target = AbilityTarget::Self;
    else if (targetType == "ally") spec.target = AbilityTarget::Ally;
    else if (targetType == "enemy") spec.target = AbilityTarget::Enemy;
    else if (targetType == "point") spec.target = AbilityTarget::Point;
    else return failure<void>(DiagnosticCode::InvalidArgument, "RTS ability target type is invalid", "targetType");
    const auto range = numberField("range", 0.0);
    const auto radius = numberField("radius", 0.0);
    const auto cooldown = numberField("cooldown", 0.0);
    const auto damage = numberField("damage", 0.0);
    const auto healing = numberField("healing", 0.0);
    const auto castTime = numberField("castTime", 0.0);
    const auto tickInterval = numberField("tickInterval", 0.0);
    const auto resourceCost = numberField("resourceCost", 0.0);
    const auto interrupt = boolField("interruptOnDamage", true);
    if (!range || !radius || !cooldown || !damage || !healing || !castTime || !tickInterval ||
        !resourceCost || !interrupt || std::floor(*resourceCost) != *resourceCost)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS ability definition contains an invalid field", "ability");
    spec.range = static_cast<float>(*range);
    spec.radius = static_cast<float>(*radius);
    spec.cooldown = static_cast<float>(*cooldown);
    spec.damage = static_cast<float>(*damage);
    spec.healing = static_cast<float>(*healing);
    spec.castTime = static_cast<float>(*castTime);
    spec.channelTickInterval = static_cast<float>(*tickInterval);
    spec.resourceCost = static_cast<std::int64_t>(*resourceCost);
    spec.resourceType = textField("resourceType");
    spec.damageType = textField("damageType", "normal");
    spec.interruptOnDamage = *interrupt;
    const std::string statusEffect = textField("statusEffect");
    if (!statusEffect.empty()) {
        auto effectDefinition = resolveEffectDefinition(
            *definitions_, statusEffect, caster.identity()->subject);
        if (!effectDefinition) return Result<void>::failure(effectDefinition.status());
        spec.appliesEffect = true;
        spec.effect = std::move(effectDefinition).takeValue();
    }
    ecs::EntityHandle targetHandle{};
    if (target.isValid()) {
        ecs::Entity* entity = findUnit(target);
        if (entity == nullptr) entity = findBuilding(target);
        if (entity == nullptr)
            return failure<void>(DiagnosticCode::NotFound, "RTS ability target was not found", "target");
        targetHandle = ecs::handle_of(entity);
    }
    const AbilityResourceDebit debit = [this](Unit& unit, const resource::CostSpec& cost)
        -> Result<resource::Receipt> {
        auto* faction = dynamic_cast<Faction*>(unit.faction()->link.resolve());
        if (faction == nullptr || !scriptRuntime_)
            return failure<resource::Receipt>(DiagnosticCode::StaleHandle,
                                              "RTS ability caster faction is stale", "caster.faction");
        const auto economy = scriptRuntime_->economies.find(faction->identity()->subject.format());
        if (economy == scriptRuntime_->economies.end())
            return failure<resource::Receipt>(DiagnosticCode::NotFound,
                                              "RTS ability economy was not found", "caster.faction");
        return economy->second->account.debit(cost);
    };
    const DamageEventSink damageEvents = [this](const combat::DamageRequest& request,
                                                 const combat::DamageOutcome& outcome,
                                                 SimulationTick tick, DamageChannel channel) {
        recordDamageEvent(request, outcome, tick, channel);
    };
    const LifecycleEventSink lifecycleEvents = [this](const LifecycleEvent& event, SimulationTick tick) {
        recordLifecycleEvent(event, tick);
    };
    return AbilitySystem::cast(caster, spec, targetHandle, point, scriptRuntime_->damage, debit,
                               damageEvents, SimulationTick(scriptTick()), lifecycleEvents);
}

Result<void> RTS::cancelScriptAbility(Unit& caster) {
    if (!owns(units_, caster))
        return failure<void>(DiagnosticCode::StaleHandle,
            "RTS ability caster does not belong to this facade", "caster");
    if (!caster.abilities()->channel)
        return failure<void>(DiagnosticCode::NotFound, "RTS unit has no active ability channel", "ability.channel");
    const auto channel = *caster.abilities()->channel;
    SubjectRef cancelledTarget;
    if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(channel.target)))
        cancelledTarget = unit->identity()->subject;
    else if (auto* building = dynamic_cast<Building*>(ecs::try_get(channel.target)))
        cancelledTarget = building->identity()->subject;
    caster.abilities()->channel.reset();
    recordLifecycleEvent({LifecycleEventKind::AbilityChannelCancelled, caster.identity()->subject,
                          cancelledTarget, channel.spec.id,
                          channel.remaining}, SimulationTick(scriptTick()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> RTS::requestFireSupport(
    Unit& requester, WorldPosition center, float radius, int shotsPerResponder,
    std::size_t maxResponders) const {
    if (!owns(units_, requester))
        return failure<std::size_t>(DiagnosticCode::StaleHandle,
            "RTS fire-support requester does not belong to this facade", "requester");
    return FireSupportSystem::request(
        requester, center, radius, shotsPerResponder, maxResponders);
}

Result<std::size_t> RTS::cancelFireSupport(Unit& requester) const {
    if (!owns(units_, requester))
        return failure<std::size_t>(DiagnosticCode::StaleHandle,
            "RTS fire-support requester does not belong to this facade", "requester");
    return FireSupportSystem::cancel(requester);
}

Result<std::size_t> RTS::unloadTransport(Unit& transport, WorldPosition destination) const {
    if (!transport.identity()->subject.isValid() || findUnit(transport.identity()->subject) != &transport)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS transport is not owned by this composition root", "transport");
    return ContainmentSystem::unload(transport, destination);
}

Result<std::size_t> RTS::evacuateBuilding(Building& building, WorldPosition destination) const {
    if (!building.identity()->subject.isValid() || findBuilding(building.identity()->subject) != &building)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS building is not owned by this composition root", "building");
    return ContainmentSystem::evacuate(building, destination);
}

Result<void> RTS::setUnitCloaked(Unit& unit, bool cloaked) const {
    if (!unit.identity()->subject.isValid() || findUnit(unit.identity()->subject) != &unit)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS unit is not owned by this composition root", "unit");
    unit.vision()->cloaked = cloaked;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::queueScriptCommand(RTSReplayCommand command) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict,
                             "RTS script world must be configured before queuing commands", "scriptWorld");
    return scriptRuntime_->commandLog.queue(std::move(command), SimulationTick{scriptRuntime_->nextTick});
}

Result<std::string> RTS::exportScriptCommandLog() const {
    if (!scriptRuntime_)
        return failure<std::string>(DiagnosticCode::Conflict,
                                    "RTS script runtime is not configured", "scriptWorld");
    return Result<std::string>::success(scriptRuntime_->commandLog.exportText(),
                                        Status::success(StatusCode::Applied));
}

Result<void> RTS::importScriptCommandLog(std::string_view text, bool clearExisting) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict,
                             "RTS script world must be configured before importing commands", "scriptWorld");
    return scriptRuntime_->commandLog.importText(
        text, SimulationTick{scriptRuntime_->nextTick}, clearExisting);
}

std::uint64_t RTS::scriptTick() const noexcept {
    return scriptRuntime_ == nullptr || scriptRuntime_->nextTick == 0 ? 0 : scriptRuntime_->nextTick - 1;
}

Result<void> RTS::captureScriptCheckpoint(std::string name) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict,
                             "RTS script world must be configured before capturing a checkpoint", "scriptWorld");
    if (name.empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "RTS checkpoint name must not be empty", "name");
    auto roots = snapshotState();
    if (!roots) return Result<void>::failure(roots.status());

    ScriptRuntime::Checkpoint checkpoint;
    checkpoint.roots = std::move(roots).takeValue();
    checkpoint.pendingProductionSubjects = scriptRuntime_->pendingProductionSubjects;
    checkpoint.paidProduction = scriptRuntime_->paidProduction;
    checkpoint.paidConstruction = scriptRuntime_->paidConstruction;
    checkpoint.commandLog = scriptRuntime_->commandLog;
    checkpoint.nextTick = scriptRuntime_->nextTick;
    checkpoint.aiProductionSequence = scriptRuntime_->aiProductionSequence;
    for (const auto& [key, slot] : scriptRuntime_->economies)
        checkpoint.economies.emplace(key, slot->ledger.snapshot());
    for (const auto& [key, fov] : scriptRuntime_->fovs)
        if (fov) checkpoint.fovs.emplace(key, fov->snapshot());
    checkpoint.navigationCosts.reserve(static_cast<std::size_t>(scriptRuntime_->width * scriptRuntime_->height));
    for (int y = 0; y < scriptRuntime_->height; ++y)
        for (int x = 0; x < scriptRuntime_->width; ++x)
            checkpoint.navigationCosts.push_back(scriptRuntime_->pathfinder.getCellCost(x, y));
    checkpoint.terrainElevations = scriptRuntime_->terrainElevations;
    scriptRuntime_->checkpoints.insert_or_assign(std::move(name), std::move(checkpoint));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::restoreScriptCheckpoint(std::string_view name) {
    if (!scriptRuntime_ || !scriptRuntime_->configured)
        return failure<void>(DiagnosticCode::Conflict,
                             "RTS script world must be configured before restoring a checkpoint", "scriptWorld");
    const auto found = scriptRuntime_->checkpoints.find(std::string(name));
    if (found == scriptRuntime_->checkpoints.end())
        return failure<void>(DiagnosticCode::NotFound, "RTS script checkpoint was not found", "name");
    const auto& checkpoint = found->second;
    const auto expectedCells = static_cast<std::size_t>(scriptRuntime_->width * scriptRuntime_->height);
    if (checkpoint.navigationCosts.size() != expectedCells || checkpoint.terrainElevations.size() != expectedCells)
        return failure<void>(DiagnosticCode::Conflict, "RTS checkpoint grid dimensions do not match", "grid");
    for (const auto& [key, snapshot] : checkpoint.fovs) {
        const auto current = scriptRuntime_->fovs.find(key);
        if (current == scriptRuntime_->fovs.end() || !current->second ||
            snapshot.width != scriptRuntime_->width || snapshot.height != scriptRuntime_->height)
            return failure<void>(DiagnosticCode::Conflict, "RTS checkpoint fog topology does not match", "fog");
    }
    const auto containsPlayer = [this](SubjectRef subject) {
        return std::any_of(players_.begin(), players_.end(), [subject](const ecs::EntityHandle& handle) {
            auto* player = dynamic_cast<Player*>(ecs::try_get(handle));
            return player != nullptr && player->identity()->subject == subject;
        });
    };
    const bool exactTopology = checkpoint.roots.units.size() == unitCount() &&
        checkpoint.roots.buildings.size() == buildingCount() &&
        checkpoint.roots.resourceNodes.size() == resourceNodeCount() &&
        checkpoint.roots.players.size() == playerCount() &&
        checkpoint.roots.factions.size() == factionCount() &&
        checkpoint.roots.matches.size() == matchCount() &&
        std::all_of(checkpoint.roots.units.begin(), checkpoint.roots.units.end(),
                    [this](const auto& value) { return findUnit(value.subject) != nullptr; }) &&
        std::all_of(checkpoint.roots.buildings.begin(), checkpoint.roots.buildings.end(),
                    [this](const auto& value) { return findBuilding(value.subject) != nullptr; }) &&
        std::all_of(checkpoint.roots.resourceNodes.begin(), checkpoint.roots.resourceNodes.end(),
                    [this](const auto& value) { return findResourceNode(value.subject) != nullptr; }) &&
        std::all_of(checkpoint.roots.players.begin(), checkpoint.roots.players.end(),
                    [&](const auto& value) { return containsPlayer(value.subject); }) &&
        std::all_of(checkpoint.roots.factions.begin(), checkpoint.roots.factions.end(),
                    [this](const auto& value) { return findFaction(value.subject) != nullptr; }) &&
        std::all_of(checkpoint.roots.matches.begin(), checkpoint.roots.matches.end(),
                    [this](const auto& value) { return findMatch(value.subject) != nullptr; });

    if (exactTopology) {
        auto restored = restoreState(checkpoint.roots);
        if (!restored) return restored;
    } else {
        // Validate the complete graph before touching the live roots. The staged
        // module also verifies definition-backed weapon materialization.
        {
            RTS validator;
            validator.setDefinitionRegistry(&scriptRuntime_->definitions);
            auto valid = validator.rebuildState(checkpoint.roots);
            if (!valid) return valid;
        }
        auto rollbackRoots = snapshotState();
        if (!rollbackRoots) return Result<void>::failure(rollbackRoots.status());
        setCrowdProvider(nullptr);
        setCombatProviders(nullptr, nullptr);
        clearOwnedRoots();
        auto rebuilt = rebuildState(checkpoint.roots);
        if (!rebuilt) {
            const Status failureStatus = rebuilt.status();
            clearOwnedRoots();
            auto rollback = rebuildState(rollbackRoots.value());
            setCrowdProvider(&scriptRuntime_->crowd);
            setCombatProviders(&scriptRuntime_->sensing, &scriptRuntime_->damage);
            auto rebound = rebindScriptRootProviders();
            if (!rollback || !rebound)
                return failure<void>(DiagnosticCode::Failed,
                                     "RTS checkpoint restore and live-state rollback both failed", "checkpoint");
            return Result<void>::failure(failureStatus);
        }
        setCrowdProvider(&scriptRuntime_->crowd);
        setCombatProviders(&scriptRuntime_->sensing, &scriptRuntime_->damage);
        auto rebound = rebindScriptRootProviders();
        if (!rebound) return rebound;
    }

    std::size_t cell = 0;
    for (int y = 0; y < scriptRuntime_->height; ++y) {
        for (int x = 0; x < scriptRuntime_->width; ++x, ++cell) {
            const float cost = checkpoint.navigationCosts[cell];
            const bool blocked = cost <= 0.0f;
            scriptRuntime_->pathfinder.setBlocked(x, y, blocked);
            scriptRuntime_->crowd.setBlocked(x, y, blocked);
            if (!blocked) {
                scriptRuntime_->pathfinder.setCellCost(x, y, cost);
                scriptRuntime_->crowd.setCellCost(x, y, cost);
            }
            for (auto& [faction, fov] : scriptRuntime_->fovs) {
                (void)faction;
                if (fov) fov->setOpaque(x, y, blocked);
            }
        }
    }
    scriptRuntime_->terrainElevations = checkpoint.terrainElevations;
    for (auto& [faction, fov] : scriptRuntime_->fovs) {
        (void)faction;
        if (!fov) continue;
        fov->setMode("heightmap");
        fov->setEyeOffset(1.0f);
        fov->setCliffBlock(0.0f);
        for (int y = 0; y < scriptRuntime_->height; ++y)
            for (int x = 0; x < scriptRuntime_->width; ++x)
                fov->setElevation(x, y, checkpoint.terrainElevations[
                    static_cast<std::size_t>(y * scriptRuntime_->width + x)]);
    }
    std::erase_if(scriptRuntime_->economies, [&](const auto& entry) {
        return !checkpoint.economies.contains(entry.first);
    });
    std::erase_if(scriptRuntime_->fovs, [&](const auto& entry) {
        return !checkpoint.fovs.contains(entry.first);
    });
    for (const auto& [key, snapshot] : checkpoint.economies) {
        const auto current = scriptRuntime_->economies.find(key);
        if (current == scriptRuntime_->economies.end())
            return failure<void>(DiagnosticCode::Conflict, "RTS checkpoint economy topology does not match", "economy");
        current->second->ledger.restore(snapshot);
    }
    for (const auto& [key, snapshot] : checkpoint.fovs) (void)scriptRuntime_->fovs.at(key)->restore(snapshot);
    scriptRuntime_->pendingProductionSubjects = checkpoint.pendingProductionSubjects;
    scriptRuntime_->paidProduction = checkpoint.paidProduction;
    scriptRuntime_->paidConstruction = checkpoint.paidConstruction;
    scriptRuntime_->commandLog = checkpoint.commandLog;
    scriptRuntime_->nextTick = checkpoint.nextTick;
    scriptRuntime_->aiProductionSequence = checkpoint.aiProductionSequence;
    const SimulationTick restoredTick{checkpoint.nextTick == 0 ? 0 : checkpoint.nextTick - 1};
    for (const auto& handle : units_)
        if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle)))
            unit->effects()->values.restoreSchedulerTick(restoredTick);
    for (const auto& handle : buildings_)
        if (auto* building = dynamic_cast<Building*>(ecs::try_get(handle)))
            building->effects()->values.restoreSchedulerTick(restoredTick);
    scriptRuntime_->actions.clear();
    scriptRuntime_->adapter.clear();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> RTS::removeScriptCheckpoint(std::string_view name) {
    if (!scriptRuntime_ || scriptRuntime_->checkpoints.erase(std::string(name)) == 0)
        return failure<void>(DiagnosticCode::NotFound, "RTS script checkpoint was not found", "name");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<bool> RTS::scriptCellVisible(Faction& faction, int x, int y) const {
    if (!scriptRuntime_ || !scriptRuntime_->configured || findFaction(faction.identity()->subject) != &faction)
        return failure<bool>(DiagnosticCode::InvalidArgument,
                             "RTS fog query requires an owned faction in a configured script world", "faction");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height)
        return failure<bool>(DiagnosticCode::InvalidArgument, "RTS fog cell is outside the grid", "cell");
    const auto found = scriptRuntime_->fovs.find(faction.identity()->subject.format());
    if (found == scriptRuntime_->fovs.end() || !found->second)
        return failure<bool>(DiagnosticCode::NotFound, "RTS faction fog provider was not found", "faction");
    return Result<bool>::success(found->second->isVisible(x, y), Status::success(StatusCode::Applied));
}

Result<bool> RTS::scriptCellExplored(Faction& faction, int x, int y) const {
    if (!scriptRuntime_ || !scriptRuntime_->configured || findFaction(faction.identity()->subject) != &faction)
        return failure<bool>(DiagnosticCode::InvalidArgument,
                             "RTS fog query requires an owned faction in a configured script world", "faction");
    if (x < 0 || y < 0 || x >= scriptRuntime_->width || y >= scriptRuntime_->height)
        return failure<bool>(DiagnosticCode::InvalidArgument, "RTS fog cell is outside the grid", "cell");
    const auto found = scriptRuntime_->fovs.find(faction.identity()->subject.format());
    if (found == scriptRuntime_->fovs.end() || !found->second)
        return failure<bool>(DiagnosticCode::NotFound, "RTS faction fog provider was not found", "faction");
    return Result<bool>::success(found->second->isExplored(x, y), Status::success(StatusCode::Applied));
}

Result<Value> RTS::scriptContact(Faction& faction, SubjectRef target) const {
    if (findFaction(faction.identity()->subject) != &faction || !target.isValid())
        return failure<Value>(DiagnosticCode::InvalidArgument,
                              "RTS contact query requires an owned faction and valid target", "contact");
    const auto* contact = FogOfWarSystem::contact(faction, target);
    if (contact == nullptr)
        return failure<Value>(DiagnosticCode::NotFound, "RTS faction contact was not found", "contact");
    return Result<Value>::success(Value(Value::Object{
        {"subject", contact->subject.format()}, {"kind", contact->kind},
        {"x", contact->position.x}, {"y", contact->position.y},
        {"age", contact->ageSeconds}, {"visible", contact->visible}, {"detected", contact->detected}}),
        Status::success(StatusCode::Applied));
}

void RTS::removeUnitRoot(Unit& unit) {
    const auto handle = ecs::handle_of(&unit);
    const SubjectRef subject = unit.identity()->subject;
    const auto passengers = unit.containment()->occupants;
    unit.containment()->occupants.clear();
    for (const auto& passengerHandle : passengers) {
        auto* passenger = dynamic_cast<Unit*>(ecs::try_get(passengerHandle));
        if (passenger != nullptr && passenger != &unit && owns(units_, *passenger))
            removeUnitRoot(*passenger);
    }

    if (crowd_ != nullptr && unit.crowd()->link.isBound())
        crowd_->removeNamedAgent(unit.crowd()->link.key());
    const std::string subjectKey = subject.format();
    if (sensing_ != nullptr)
        sensing_->remove(subjectKey).ignore("best-effort RTS sensing cleanup");
    combatState_.mirroredSubjects.erase(subjectKey);
    combatState_.blockedSubjects.erase(subjectKey);

    for (const auto& nodeHandle : resourceNodes_) {
        if (auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(nodeHandle)))
            std::erase_if(node->harvest()->workers,
                [&](const auto& value) { return sameHandle(value, handle); });
    }
    for (const auto& buildingHandle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(buildingHandle));
        if (building == nullptr) continue;
        std::erase_if(building->construction()->builders,
            [&](const auto& value) { return sameHandle(value, handle); });
        std::erase_if(building->garrison()->occupants,
            [&](const auto& value) { return sameHandle(value, handle); });
        std::erase_if(building->rally()->reinforcements,
            [&](const auto& value) { return sameHandle(value, handle); });
        if (sameHandle(building->rally()->transport, handle)) {
            building->rally()->transport = {};
            building->rally()->transportActive = false;
        }
        building->capture()->blockedByGarrison = !building->garrison()->occupants.empty();
    }
    for (const auto& otherHandle : units_) {
        auto* other = dynamic_cast<Unit*>(ecs::try_get(otherHandle));
        if (other == nullptr || other == &unit) continue;
        std::erase_if(other->containment()->occupants,
            [&](const auto& value) { return sameHandle(value, handle); });
        if (other->containment()->container.isBound() &&
            sameHandle(other->containment()->container.handle(), handle))
            other->containment()->container = {};
        if (sameHandle(other->combat()->target, handle)) other->combat()->target = {};
        if (sameHandle(other->tactics()->escortTarget, handle)) other->tactics()->escortTarget = {};
        if (sameHandle(other->supply()->assignedTarget, handle)) other->supply()->assignedTarget = {};
        if (sameHandle(other->supply()->convoyLeader, handle)) other->supply()->convoyLeader = {};
    }
    for (const auto& factionHandle : factions_) {
        if (auto* faction = dynamic_cast<Faction*>(ecs::try_get(factionHandle)))
            std::erase_if(faction->members()->units,
                [&](const auto& value) { return sameHandle(value, handle); });
    }
    for (const auto& playerHandle : players_) {
        if (auto* player = dynamic_cast<Player*>(ecs::try_get(playerHandle)))
            std::erase_if(player->selection()->units,
                [&](const auto& value) { return sameHandle(value, handle); });
    }

    const auto weaponHandle = unit.weapon()->link.handle();
    if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(unit.weapon()->link.resolve())) weapon->release();
    std::erase_if(weapons_, [&](const auto& value) { return sameHandle(value, weaponHandle); });
    std::erase_if(units_, [&](const auto& value) { return sameHandle(value, handle); });
    if (scriptRuntime_)
        std::erase_if(scriptRuntime_->paidProduction,
            [&](const auto& value) { return value.resultSubject == subject; });
    unit.release();
}

void RTS::removeBuildingRoot(Building& building, bool destroyOccupants) {
    const auto handle = ecs::handle_of(&building);
    const SubjectRef subject = building.identity()->subject;
    const std::string subjectKey = subject.format();
    if (sensing_ != nullptr)
        sensing_->remove(subjectKey).ignore("best-effort RTS sensing cleanup");
    combatState_.mirroredSubjects.erase(subjectKey);
    combatState_.blockedSubjects.erase(subjectKey);
    const auto occupants = building.garrison()->occupants;
    building.garrison()->occupants.clear();
    building.capture()->blockedByGarrison = false;
    for (const auto& occupantHandle : occupants) {
        auto* occupant = dynamic_cast<Unit*>(ecs::try_get(occupantHandle));
        if (occupant == nullptr || !owns(units_, *occupant)) continue;
        occupant->containment()->container = {};
        if (destroyOccupants) {
            occupant->durability()->alive = false;
            occupant->durability()->state.health = 0.0;
            removeUnitRoot(*occupant);
        } else {
            occupant->motion()->x = building.placement()->worldX;
            occupant->motion()->y = building.placement()->worldY;
            occupant->motion()->arrived = true;
            occupant->orders()->values.clear();
        }
    }
    for (const auto& unitHandle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(unitHandle));
        if (unit == nullptr) continue;
        if (unit->containment()->container.isBound() &&
            sameHandle(unit->containment()->container.handle(), handle))
            unit->containment()->container = {};
        if (sameHandle(unit->combat()->target, handle)) unit->combat()->target = {};
        if (sameHandle(unit->tactics()->escortTarget, handle)) unit->tactics()->escortTarget = {};
        if (unit->worker()->dropoff.isBound() && sameHandle(unit->worker()->dropoff.handle(), handle))
            unit->worker()->dropoff = {};
    }
    for (const auto& factionHandle : factions_) {
        if (auto* faction = dynamic_cast<Faction*>(ecs::try_get(factionHandle)))
            std::erase_if(faction->members()->buildings,
                [&](const auto& value) { return sameHandle(value, handle); });
    }
    for (const auto& playerHandle : players_) {
        if (auto* player = dynamic_cast<Player*>(ecs::try_get(playerHandle)))
            std::erase_if(player->selection()->buildings,
                [&](const auto& value) { return sameHandle(value, handle); });
    }
    const auto weaponHandle = building.weapon()->link.handle();
    if (auto* weapon = dynamic_cast<weapon::WeaponEntity*>(building.weapon()->link.resolve())) weapon->release();
    std::erase_if(weapons_, [&](const auto& value) { return sameHandle(value, weaponHandle); });
    std::erase_if(buildings_, [&](const auto& value) { return sameHandle(value, handle); });
    if (scriptRuntime_) {
        std::erase_if(scriptRuntime_->paidConstruction,
            [&](const auto& value) { return value.building == subject; });
        std::erase_if(scriptRuntime_->paidProduction, [&](const auto& value) {
            if (value.producer != subject) return false;
            scriptRuntime_->pendingProductionSubjects.erase(value.taskId);
            return true;
        });
    }
    building.release();
}

void RTS::removeResourceNodeRoot(ResourceNode& node) {
    const auto handle = ecs::handle_of(&node);
    for (const auto& workerHandle : node.harvest()->workers) {
        auto* worker = dynamic_cast<Unit*>(ecs::try_get(workerHandle));
        if (worker != nullptr && worker->worker()->resourceNode.isBound() &&
            sameHandle(worker->worker()->resourceNode.handle(), handle)) {
            worker->worker()->resourceNode = {};
            worker->orders()->values.clear();
        }
    }
    node.harvest()->workers.clear();
    std::erase_if(resourceNodes_, [&](const auto& value) { return sameHandle(value, handle); });
    node.release();
}

Result<void> RTS::remove(SubjectRef subject) {
    if (!subject.isValid())
        return failure<void>(DiagnosticCode::InvalidArgument,
            "RTS removal requires a valid stable identity", "subject");
    if (auto* unit = findUnit(subject)) {
        removeUnitRoot(*unit);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (auto* building = findBuilding(subject)) {
        removeBuildingRoot(*building, false);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    if (auto* node = findResourceNode(subject)) {
        removeResourceNodeRoot(*node);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return failure<void>(DiagnosticCode::NotFound,
        "RTS gameplay root identity was not found", "subject");
}

Result<std::size_t> RTS::cleanupDestroyed() {
    const std::size_t before = unitCount() + buildingCount();
    std::vector<ecs::EntityHandle> deadUnits;
    std::vector<ecs::EntityHandle> deadBuildings;
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit != nullptr && (!unit->durability()->alive || unit->durability()->state.health <= 0.0))
            deadUnits.push_back(handle);
    }
    for (const auto& handle : buildings_) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building != nullptr && (!building->integrity()->alive || building->integrity()->state.health <= 0.0))
            deadBuildings.push_back(handle);
    }
    for (const auto& handle : deadBuildings) {
        auto* building = dynamic_cast<Building*>(ecs::try_get(handle));
        if (building == nullptr || !owns(buildings_, *building)) continue;
        removeBuildingRoot(*building, true);
    }
    for (const auto& handle : deadUnits) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (unit == nullptr || !owns(units_, *unit)) continue;
        removeUnitRoot(*unit);
    }
    const std::size_t after = unitCount() + buildingCount();
    const std::size_t removed = before >= after ? before - after : 0;
    return Result<std::size_t>::success(removed,
        Status::success(removed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

std::size_t RTS::unitCount() const noexcept { return countLive<Unit>(units_); }
std::size_t RTS::buildingCount() const noexcept { return countLive<Building>(buildings_); }
std::size_t RTS::resourceNodeCount() const noexcept { return countLive<ResourceNode>(resourceNodes_); }
std::size_t RTS::playerCount() const noexcept { return countLive<Player>(players_); }
std::size_t RTS::factionCount() const noexcept { return countLive<Faction>(factions_); }
std::size_t RTS::matchCount() const noexcept { return countLive<Match>(matches_); }

Faction* RTS::findFaction(SubjectRef subject) const noexcept { return findSubject<Faction>(factions_, subject); }
Match* RTS::findMatch(SubjectRef subject) const noexcept { return findSubject<Match>(matches_, subject); }

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
    cls.addFunc("resourceNodeCount", [](RTS* self) { return self->resourceNodeCount(); });
    cls.addFunc("playerCount", [](RTS* self) { return self->playerCount(); });
    cls.addFunc("factionCount", [](RTS* self) { return self->factionCount(); });
    cls.addFunc("matchCount", [](RTS* self) { return self->matchCount(); });
    cls.addFunc("scriptTick", [](RTS* self) { return static_cast<std::int64_t>(self->scriptTick()); });
    const auto vm = cls.getHandle();
    cls.addFunc("removeSubject", [vm](RTS* self, const std::string& subjectText) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "subject");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        return script::projectResult(vm, self->remove(std::move(subject).takeValue()));
    });
    cls.addFunc("newFaction", [vm](RTS* self, const std::string& subjectText) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "subject");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        return script::projectResult(vm, self->newFaction(std::move(subject).takeValue()),
            [](Faction* faction) { return Value(faction->identity()->subject.format()); });
    });
    cls.addFunc("newMatch", [vm](RTS* self, const std::string& subjectText) -> ssq::Table {
        auto subjectValue = parseScriptSubject(subjectText, "subject");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        return script::projectResult(vm, self->newMatch(std::move(subjectValue).takeValue()),
            [](Match* match) { return Value(match->identity()->subject.format()); });
    });
    cls.addFunc("configureMatch", [vm](RTS* self, const std::string& matchText,
                                        const std::string& ruleText, const std::string& archetype,
                                        double targetValue) -> ssq::Table {
        auto matchSubject = parseScriptSubject(matchText, "match");
        if (!matchSubject) return script::projectStatusResult(vm, matchSubject.status(), false, false);
        Match* match = self->findMatch(matchSubject.value());
        if (match == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS match identity was not found", "match")), false, false);
        VictoryRule rule;
        if (ruleText == "annihilation") rule = VictoryRule::Annihilation;
        else if (ruleText == "headquarters") rule = VictoryRule::DestroyHeadquarters;
        else if (ruleText == "resource") rule = VictoryRule::ResourceTarget;
        else return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS victory rule is invalid", "rule")), false, false);
        return script::projectResult(vm, self->configureMatch(*match, rule, archetype, targetValue));
    });
    cls.addFunc("addMatchParticipant", [vm](RTS* self, const std::string& matchText,
                                             const std::string& factionText, int team) -> ssq::Table {
        auto matchSubject = parseScriptSubject(matchText, "match");
        if (!matchSubject) return script::projectStatusResult(vm, matchSubject.status(), false, false);
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Match* match = self->findMatch(matchSubject.value());
        Faction* faction = self->findFaction(factionSubject.value());
        if (match == nullptr || faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS match participant identity was not found", "participant")),
                false, false);
        return script::projectResult(vm, self->addMatchParticipant(*match, *faction, team));
    });
    cls.addFunc("startMatch", [vm](RTS* self, const std::string& matchText) -> ssq::Table {
        auto matchSubject = parseScriptSubject(matchText, "match");
        if (!matchSubject) return script::projectStatusResult(vm, matchSubject.status(), false, false);
        Match* match = self->findMatch(matchSubject.value());
        if (match == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS match identity was not found", "match")), false, false);
        return script::projectResult(vm, self->startMatch(*match));
    });
    cls.addFunc("surrenderMatch", [vm](RTS* self, const std::string& matchText,
                                        const std::string& factionText) -> ssq::Table {
        auto matchSubject = parseScriptSubject(matchText, "match");
        if (!matchSubject) return script::projectStatusResult(vm, matchSubject.status(), false, false);
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Match* match = self->findMatch(matchSubject.value());
        Faction* faction = self->findFaction(factionSubject.value());
        if (match == nullptr || faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS match participant identity was not found", "participant")),
                false, false);
        return script::projectResult(vm, self->surrenderMatch(*match, *faction));
    });
    cls.addFunc("inspectMatch", [vm](RTS* self, const std::string& matchText) -> ssq::Table {
        auto matchSubject = parseScriptSubject(matchText, "match");
        if (!matchSubject) return script::projectStatusResult(vm, matchSubject.status(), false, false);
        Match* match = self->findMatch(matchSubject.value());
        if (match == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS match identity was not found", "match")), false, false);
        return script::projectResult(vm, self->inspectMatch(*match), [](Value value) { return value; });
    });
    cls.addFunc("newUnit", [vm](RTS* self, const std::string& subjectText, const std::string& definitionText,
                                const std::string& factionText, float x, float y) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "subject");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto definition = parseScriptDefinition(definitionText);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Faction* faction = self->findFaction(std::move(factionSubject).takeValue());
        if (faction == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script faction was not found", "faction")),
                false, false);
        auto created = self->newFactionUnit(*faction, std::move(subject).takeValue(),
                                            std::move(definition).takeValue());
        if (!created) return script::projectStatusResult(vm, created.status(), false, false);
        Unit* unit = std::move(created).takeValue();
        unit->motion()->x = x;
        unit->motion()->y = y;
        return script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, true,
                                           Value(unit->identity()->subject.format()));
    });
    cls.addFunc("newBuilding", [vm](RTS* self, const std::string& subjectText, const std::string& definitionText,
                                    const std::string& factionText, float x, float y) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "subject");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto definition = parseScriptDefinition(definitionText);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Faction* faction = self->findFaction(std::move(factionSubject).takeValue());
        if (faction == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script faction was not found", "faction")),
                false, false);
        auto created = self->newFactionBuilding(*faction, std::move(subject).takeValue(),
                                                std::move(definition).takeValue());
        if (!created) return script::projectStatusResult(vm, created.status(), false, false);
        Building* building = std::move(created).takeValue();
        building->placement()->worldX = x;
        building->placement()->worldY = y;
        return script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, true,
                                           Value(building->identity()->subject.format()));
    });
    cls.addFunc("newResourceNode", [vm](RTS* self, const std::string& subjectText, const std::string& resource,
                                        float amount, float x, float y, int capacity) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "subject");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        if (capacity <= 0)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "RTS script resource capacity must be positive", "capacity")),
                false, false);
        return script::projectResult(vm,
            self->newResourceNode(std::move(subject).takeValue(), resource, amount, {x, y},
                                  static_cast<std::size_t>(capacity)),
            [](ResourceNode* node) { return Value(node->identity()->subject.format()); });
    });
    cls.addFunc("inspectState", [vm](RTS* self) -> ssq::Table {
        return script::projectStatusResult(vm, Status::success(), true, true, self->inspectState());
    });
    cls.addFunc("inspectFrameEvents", [vm](RTS* self) -> ssq::Table {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           self->inspectFrameEvents());
    });
    cls.addFunc("stepScript", [vm](RTS* self, double seconds) -> ssq::Table {
        return script::projectResult(vm, self->stepScript(seconds), [](std::size_t processed) {
            return Value(static_cast<std::int64_t>(processed));
        });
    });
    cls.addFunc("configureScriptWorld", [vm](RTS* self, int width, int height, float cellSize,
                                              float originX, float originY) -> ssq::Table {
        return script::projectResult(vm, self->configureScriptWorld(width, height, cellSize, originX, originY));
    });
    cls.addFunc("captureScriptCheckpoint", [vm](RTS* self, const std::string& name) -> ssq::Table {
        return script::projectResult(vm, self->captureScriptCheckpoint(name));
    });
    cls.addFunc("restoreScriptCheckpoint", [vm](RTS* self, const std::string& name) -> ssq::Table {
        return script::projectResult(vm, self->restoreScriptCheckpoint(name));
    });
    cls.addFunc("removeScriptCheckpoint", [vm](RTS* self, const std::string& name) -> ssq::Table {
        return script::projectResult(vm, self->removeScriptCheckpoint(name));
    });
    cls.addFunc("loadScriptContent", [vm](RTS* self, const std::string& json) -> ssq::Table {
        return script::projectResult(vm, self->loadScriptContent(json), [](ContentImportReceipt receipt) {
            return Value(Value::Object{{"inserted", static_cast<std::int64_t>(receipt.inserted)},
                                       {"replaced", static_cast<std::int64_t>(receipt.replaced)}});
        });
    });
    cls.addFunc("setScriptNavigationBlocked", [vm](RTS* self, int x, int y, bool blocked) -> ssq::Table {
        return script::projectResult(vm, self->setScriptNavigationBlocked(x, y, blocked));
    });
    cls.addFunc("setScriptNavigationCost", [vm](RTS* self, int x, int y, float cost) -> ssq::Table {
        return script::projectResult(vm, self->setScriptNavigationCost(x, y, cost));
    });
    cls.addFunc("setScriptTerrainElevation", [vm](RTS* self, int x, int y, float elevation) -> ssq::Table {
        return script::projectResult(vm, self->setScriptTerrainElevation(x, y, elevation));
    });
    cls.addFunc("scriptTerrainElevation", [vm](RTS* self, int x, int y) -> ssq::Table {
        return script::projectResult(vm, self->scriptTerrainElevation(x, y),
                                     [](float elevation) { return Value(static_cast<double>(elevation)); });
    });
    cls.addFunc("scriptCellVisible", [vm](RTS* self, const std::string& factionText,
                                           int x, int y) -> ssq::Table {
        auto subjectValue = parseScriptSubject(factionText, "faction");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Faction* faction = self->findFaction(subjectValue.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS fog faction identity was not found", "faction")), false, false);
        return script::projectResult(vm, self->scriptCellVisible(*faction, x, y),
            [](bool visible) { return Value(visible); });
    });
    cls.addFunc("scriptCellExplored", [vm](RTS* self, const std::string& factionText,
                                            int x, int y) -> ssq::Table {
        auto subjectValue = parseScriptSubject(factionText, "faction");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Faction* faction = self->findFaction(subjectValue.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS fog faction identity was not found", "faction")), false, false);
        return script::projectResult(vm, self->scriptCellExplored(*faction, x, y),
            [](bool explored) { return Value(explored); });
    });
    cls.addFunc("scriptContact", [vm](RTS* self, const std::string& factionText,
                                       const std::string& targetText) -> ssq::Table {
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        auto targetSubject = parseScriptSubject(targetText, "target");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        Faction* faction = self->findFaction(factionSubject.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS fog faction identity was not found", "faction")), false, false);
        return script::projectResult(vm, self->scriptContact(*faction, targetSubject.value()),
            [](Value value) { return value; });
    });
    cls.addFunc("addScriptResource", [vm](RTS* self, const std::string& factionText,
                                          const std::string& resource, std::int64_t amount) -> ssq::Table {
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Faction* faction = self->findFaction(std::move(factionSubject).takeValue());
        if (faction == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script faction was not found", "faction")), false, false);
        return script::projectResult(vm, self->addScriptResource(*faction, resource, amount));
    });
    cls.addFunc("scriptResource", [vm](RTS* self, const std::string& factionText,
                                       const std::string& resource) -> ssq::Table {
        auto factionSubject = parseScriptSubject(factionText, "faction");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        Faction* faction = self->findFaction(std::move(factionSubject).takeValue());
        if (faction == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script faction was not found", "faction")), false, false);
        return script::projectResult(vm, self->scriptResource(*faction, resource),
                                     [](std::int64_t amount) { return Value(amount); });
    });
    cls.addFunc("configureScriptAI", [vm](RTS* self, const std::string& factionText,
        const std::string& workerText, const std::string& armyText,
        const std::string& targetBuildingText, int desiredWorkers, int attackThreshold,
        float thinkInterval, float formationSpacing, bool enabled) -> ssq::Table {
        auto factionSubject = parseScriptSubject(factionText, "faction");
        auto worker = parseScriptDefinition(workerText);
        auto army = parseScriptDefinition(armyText);
        auto target = parseScriptDefinition(targetBuildingText);
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        if (!worker) return script::projectStatusResult(vm, worker.status(), false, false);
        if (!army) return script::projectStatusResult(vm, army.status(), false, false);
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        Faction* faction = self->findFaction(factionSubject.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS script AI faction was not found", "faction")), false, false);
        return script::projectResult(vm, self->configureScriptAI(*faction, worker.value(), army.value(),
            target.value(), desiredWorkers, attackThreshold, thinkInterval, formationSpacing, enabled));
    });
    cls.addFunc("queueScriptUnit", [vm](RTS* self, const std::string& producerText,
                                         const std::string& unitSubjectText,
                                         const std::string& definitionText, int priority) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script producer was not found", "producer")), false, false);
        auto unitSubject = parseScriptSubject(unitSubjectText, "unitSubject");
        if (!unitSubject) return script::projectStatusResult(vm, unitSubject.status(), false, false);
        auto definition = parseScriptDefinition(definitionText);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        return script::projectResult(vm,
            self->queueScriptUnit(*producer, unitSubject.value(), definition.value(), priority),
            [](RTSBuildReceipt receipt) {
                return Value(Value::Object{{"productionTaskId", std::move(receipt.productionTaskId)},
                                           {"orderId", std::move(receipt.orderId)}});
            });
    });
    cls.addFunc("queueScriptReinforcement", [vm](RTS* self, const std::string& producerText,
        const std::string& unitSubjectText, const std::string& definitionText,
        int priority) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        auto unitSubject = parseScriptSubject(unitSubjectText, "unitSubject");
        auto definition = parseScriptDefinition(definitionText);
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        if (!unitSubject) return script::projectStatusResult(vm, unitSubject.status(), false, false);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS script reinforcement producer was not found", "producer")),
                false, false);
        return script::projectResult(vm,
            self->queueScriptReinforcement(*producer, unitSubject.value(), definition.value(), priority),
            [](ReinforcementRequestReceipt receipt) {
                return Value(Value::Object{{"requestedProduct", std::move(receipt.requestedProduct)},
                                           {"queuedProduct", std::move(receipt.queuedProduct)},
                                           {"productionTaskId", std::move(receipt.taskId)}});
            });
    });
    cls.addFunc("startScriptConstruction", [vm](RTS* self, const std::string& factionText,
                                                  const std::string& buildingSubjectText,
                                                  const std::string& definitionText, float x, float y,
                                                  const std::string& builderText) -> ssq::Table {
        auto factionSubject = parseScriptSubject(factionText, "faction");
        auto buildingSubject = parseScriptSubject(buildingSubjectText, "buildingSubject");
        auto definition = parseScriptDefinition(definitionText);
        auto builderSubject = parseScriptSubject(builderText, "builder");
        if (!factionSubject) return script::projectStatusResult(vm, factionSubject.status(), false, false);
        if (!buildingSubject) return script::projectStatusResult(vm, buildingSubject.status(), false, false);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        if (!builderSubject) return script::projectStatusResult(vm, builderSubject.status(), false, false);
        Faction* faction = self->findFaction(factionSubject.value());
        Unit* builder = self->findUnit(builderSubject.value());
        if (faction == nullptr || builder == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script construction faction or builder was not found",
                                                  "construction")), false, false);
        return script::projectResult(vm,
            self->startScriptConstruction(*faction, buildingSubject.value(), definition.value(), {x, y}, *builder),
            [](Building* building) { return Value(building->identity()->subject.format()); });
    });
    const auto buildingLifecycle = [vm](RTS* self, const std::string& buildingText,
                                        bool sell) -> ssq::Table {
        auto subjectValue = parseScriptSubject(buildingText, "building");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Building* building = self->findBuilding(subjectValue.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS script building was not found", "building")), false, false);
        auto result = sell ? self->sellScriptBuilding(*building)
                           : self->cancelScriptConstruction(*building);
        return script::projectResult(vm, std::move(result), [](resource::Receipt) { return Value(true); });
    };
    cls.addFunc("cancelScriptConstruction", [buildingLifecycle](RTS* self,
        const std::string& buildingText) -> ssq::Table {
        return buildingLifecycle(self, buildingText, false);
    });
    cls.addFunc("sellScriptBuilding", [buildingLifecycle](RTS* self,
        const std::string& buildingText) -> ssq::Table {
        return buildingLifecycle(self, buildingText, true);
    });
    cls.addFunc("queueScriptResearch", [vm](RTS* self, const std::string& producerText,
                                             const std::string& upgrade, int priority) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script research producer was not found", "producer")),
                false, false);
        return script::projectResult(vm, self->queueScriptResearch(*producer, upgrade, priority),
            [](RTSBuildReceipt receipt) {
                return Value(Value::Object{{"productionTaskId", std::move(receipt.productionTaskId)},
                                           {"orderId", std::move(receipt.orderId)}});
            });
    });
    cls.addFunc("cancelScriptProduction", [vm](RTS* self, const std::string& producerText,
        int queueIndex) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS script producer was not found", "producer")), false, false);
        return script::projectResult(vm, self->cancelScriptProduction(*producer, queueIndex),
            [](RTSCancelProductionReceipt receipt) {
                return Value(Value::Object{{"productionTaskId", std::move(receipt.productionTaskId)},
                                           {"orderId", std::move(receipt.orderId)}});
            });
    });
    cls.addFunc("castScriptAbility", [vm](RTS* self, const std::string& casterText,
                                           const std::string& ability, const std::string& targetText,
                                           float x, float y) -> ssq::Table {
        auto casterSubject = parseScriptSubject(casterText, "caster");
        if (!casterSubject) return script::projectStatusResult(vm, casterSubject.status(), false, false);
        Unit* caster = self->findUnit(casterSubject.value());
        if (caster == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS script ability caster was not found", "caster")),
                false, false);
        SubjectRef target;
        if (!targetText.empty()) {
            auto parsed = parseScriptSubject(targetText, "target");
            if (!parsed) return script::projectStatusResult(vm, parsed.status(), false, false);
            target = parsed.value();
        }
        return script::projectResult(vm, self->castScriptAbility(*caster, ability, target, {x, y}));
    });
    cls.addFunc("cancelScriptAbility", [vm](RTS* self, const std::string& casterText) -> ssq::Table {
        auto casterSubject = parseScriptSubject(casterText, "caster");
        if (!casterSubject) return script::projectStatusResult(vm, casterSubject.status(), false, false);
        Unit* caster = self->findUnit(casterSubject.value());
        if (caster == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS script ability caster was not found", "caster")), false, false);
        return script::projectResult(vm, self->cancelScriptAbility(*caster));
    });
    cls.addFunc("setBuildingRally", [vm](RTS* self, const std::string& producerText,
        float x, float y, bool attackMove, bool groupedReinforcements) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS rally producer was not found", "producer")), false, false);
        CommandSpec command;
        command.kind = attackMove ? OrderKind::AttackMove : OrderKind::Move;
        command.target = {x, y};
        return script::projectResult(vm, self->setBuildingRally(*producer, command, groupedReinforcements));
    });
    cls.addFunc("linkBuildingRally", [vm](RTS* self, const std::string& producerText,
        const std::string& sourceText) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        auto sourceSubject = parseScriptSubject(sourceText, "source");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        if (!sourceSubject) return script::projectStatusResult(vm, sourceSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        Building* source = self->findBuilding(sourceSubject.value());
        if (producer == nullptr || source == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS rally producer or source was not found", "building")), false, false);
        return script::projectResult(vm, self->linkBuildingRally(*producer, *source));
    });
    cls.addFunc("clearBuildingRally", [vm](RTS* self, const std::string& producerText) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS rally producer was not found", "producer")), false, false);
        return script::projectResult(vm, self->clearBuildingRally(*producer));
    });
    cls.addFunc("setReinforcementLimit", [vm](RTS* self, const std::string& producerText,
        std::int64_t maximum) -> ssq::Table {
        if (maximum < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS reinforcement limit must be non-negative", "maximum")),
                false, false);
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        return script::projectResult(vm,
            self->setReinforcementLimit(*producer, static_cast<std::size_t>(maximum)));
    });
    cls.addFunc("setReinforcementTypeLimit", [vm](RTS* self, const std::string& producerText,
        const std::string& unitType, std::int64_t maximum) -> ssq::Table {
        if (maximum < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS reinforcement type limit must be non-negative", "maximum")),
                false, false);
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        return script::projectResult(vm,
            self->setReinforcementTypeLimit(*producer, unitType, static_cast<std::size_t>(maximum)));
    });
    cls.addFunc("setReinforcementTypePriority", [vm](RTS* self, const std::string& producerText,
        const std::string& unitType, int priority) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        return script::projectResult(vm, self->setReinforcementTypePriority(*producer, unitType, priority));
    });
    cls.addFunc("setReinforcementFallback", [vm](RTS* self, const std::string& producerText,
        const std::string& preferred, const std::string& fallback) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        return script::projectResult(vm, self->setReinforcementFallback(*producer, preferred, fallback));
    });
    cls.addFunc("setReinforcementAutoCancel", [vm](RTS* self, const std::string& producerText,
        float seconds) -> ssq::Table {
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        return script::projectResult(vm, self->setReinforcementAutoCancel(*producer, seconds));
    });
    cls.addFunc("setReinforcementTransport", [vm](RTS* self, const std::string& producerText,
        const std::string& transportText, std::int64_t minimumLoad) -> ssq::Table {
        if (minimumLoad < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS reinforcement minimum load must be non-negative",
                "minimumLoad")), false, false);
        auto producerSubject = parseScriptSubject(producerText, "producer");
        if (!producerSubject) return script::projectStatusResult(vm, producerSubject.status(), false, false);
        Building* producer = self->findBuilding(producerSubject.value());
        if (producer == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reinforcement producer was not found", "producer")), false, false);
        Unit* transport = nullptr;
        if (!transportText.empty()) {
            auto transportSubject = parseScriptSubject(transportText, "transport");
            if (!transportSubject)
                return script::projectStatusResult(vm, transportSubject.status(), false, false);
            transport = self->findUnit(transportSubject.value());
            if (transport == nullptr)
                return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS reinforcement transport was not found", "transport")),
                    false, false);
        }
        return script::projectResult(vm, self->setReinforcementTransport(
            *producer, transport, static_cast<std::size_t>(minimumLoad)));
    });
    cls.addFunc("exportScriptCommandLog", [vm](RTS* self) -> ssq::Table {
        return script::projectResult(vm, self->exportScriptCommandLog(),
            [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("importScriptCommandLog", [vm](RTS* self, const std::string& text,
                                                bool clearExisting) -> ssq::Table {
        return script::projectResult(vm, self->importScriptCommandLog(text, clearExisting));
    });
    cls.addFunc("queueScriptConstructionCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& factionText, const std::string& builderText,
        const std::string& buildingSubjectText, const std::string& definitionText,
        float x, float y) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto faction = parseScriptSubject(factionText, "faction");
        auto builder = parseScriptSubject(builderText, "builder");
        auto result = parseScriptSubject(buildingSubjectText, "buildingSubject");
        auto definition = parseScriptDefinition(definitionText);
        if (!faction) return script::projectStatusResult(vm, faction.status(), false, false);
        if (!builder) return script::projectStatusResult(vm, builder.status(), false, false);
        if (!result) return script::projectStatusResult(vm, result.status(), false, false);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::Construction;
        replay.units = {builder.value()};
        replay.faction = faction.value();
        replay.resultSubject = result.value();
        replay.definition = definition.value();
        replay.point = {x, y};
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptProductionCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& producerText, const std::string& unitSubjectText,
        const std::string& definitionText, int priority) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto producer = parseScriptSubject(producerText, "producer");
        auto result = parseScriptSubject(unitSubjectText, "unitSubject");
        auto definition = parseScriptDefinition(definitionText);
        if (!producer) return script::projectStatusResult(vm, producer.status(), false, false);
        if (!result) return script::projectStatusResult(vm, result.status(), false, false);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::Production;
        replay.producer = producer.value();
        replay.resultSubject = result.value();
        replay.definition = definition.value();
        replay.priority = priority;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptReinforcementCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& producerText, const std::string& unitSubjectText,
        const std::string& definitionText, int priority) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto producer = parseScriptSubject(producerText, "producer");
        auto result = parseScriptSubject(unitSubjectText, "unitSubject");
        auto definition = parseScriptDefinition(definitionText);
        if (!producer) return script::projectStatusResult(vm, producer.status(), false, false);
        if (!result) return script::projectStatusResult(vm, result.status(), false, false);
        if (!definition) return script::projectStatusResult(vm, definition.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::ReinforcementProduction;
        replay.producer = producer.value();
        replay.resultSubject = result.value();
        replay.definition = definition.value();
        replay.priority = priority;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptResearchCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& producerText, const std::string& upgrade, int priority) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto producer = parseScriptSubject(producerText, "producer");
        if (!producer) return script::projectStatusResult(vm, producer.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::Research;
        replay.producer = producer.value();
        replay.value = upgrade;
        replay.priority = priority;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptAbilityCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& casterText, const std::string& ability,
        const std::string& targetText, float x, float y) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto caster = parseScriptSubject(casterText, "caster");
        if (!caster) return script::projectStatusResult(vm, caster.status(), false, false);
        SubjectRef target;
        if (!targetText.empty()) {
            auto parsed = parseScriptSubject(targetText, "target");
            if (!parsed) return script::projectStatusResult(vm, parsed.status(), false, false);
            target = parsed.value();
        }
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::Ability;
        replay.units = {caster.value()};
        replay.value = ability;
        replay.targetEntity = target;
        replay.point = {x, y};
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptCancelProductionCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& producerText, int queueIndex) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto producer = parseScriptSubject(producerText, "producer");
        if (!producer) return script::projectStatusResult(vm, producer.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::CancelProduction;
        replay.producer = producer.value();
        replay.priority = queueIndex;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptCancelAbilityCommand", [vm](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjectTexts) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::CancelAbility;
        replay.units = std::move(subjects).takeValue();
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptFireSupportCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& requesterText, float x, float y, float radius,
        int shotsPerResponder, int maxResponders) -> ssq::Table {
        if (tick < 0 || maxResponders < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "RTS fire-support tick and responder limit must be non-negative", "fireSupport")),
                false, false);
        auto requester = parseScriptSubject(requesterText, "requester");
        if (!requester) return script::projectStatusResult(vm, requester.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::RequestFireSupport;
        replay.producer = requester.value();
        replay.point = {x, y};
        replay.command.radius = radius;
        replay.priority = shotsPerResponder;
        replay.limit = static_cast<std::size_t>(maxResponders);
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptCancelFireSupportCommand", [vm](RTS* self, std::int64_t tick,
        const std::string& requesterText) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto requester = parseScriptSubject(requesterText, "requester");
        if (!requester) return script::projectStatusResult(vm, requester.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::CancelFireSupport;
        replay.producer = requester.value();
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    const auto queueBuildingLifecycle = [vm](RTS* self, std::int64_t tick,
        const std::string& buildingText, RTSReplayOperation operation) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto building = parseScriptSubject(buildingText, "building");
        if (!building) return script::projectStatusResult(vm, building.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = operation;
        replay.producer = building.value();
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    };
    cls.addFunc("queueScriptCancelConstructionCommand", [queueBuildingLifecycle](RTS* self,
        std::int64_t tick, const std::string& buildingText) -> ssq::Table {
        return queueBuildingLifecycle(self, tick, buildingText, RTSReplayOperation::CancelConstruction);
    });
    cls.addFunc("queueScriptSellBuildingCommand", [queueBuildingLifecycle](RTS* self,
        std::int64_t tick, const std::string& buildingText) -> ssq::Table {
        return queueBuildingLifecycle(self, tick, buildingText, RTSReplayOperation::SellBuilding);
    });
    cls.addFunc("queueScriptMove", [vm](RTS* self, std::int64_t tick,
                                         const std::vector<std::string>& subjectTexts,
                                         float x, float y) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.units = std::move(subjects).takeValue();
        replay.command.kind = OrderKind::Move;
        replay.command.target = {x, y};
        replay.formation.kind = FormationKind::Grid;
        replay.formation.spacing = 1.0f;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptAttackMove", [vm](RTS* self, std::int64_t tick,
                                               const std::vector<std::string>& subjectTexts,
                                               float x, float y) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.units = std::move(subjects).takeValue();
        replay.command.kind = OrderKind::AttackMove;
        replay.command.target = {x, y};
        replay.formation.kind = FormationKind::Grid;
        replay.formation.spacing = 1.0f;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptAttack", [vm](RTS* self, std::int64_t tick,
                                           const std::vector<std::string>& subjectTexts,
                                           const std::string& targetText) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(targetText, "target");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        ecs::Entity* target = self->findUnit(targetSubject.value());
        if (target == nullptr) target = self->findBuilding(targetSubject.value());
        if (target == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS replay target identity was not found", "target")), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.units = std::move(subjects).takeValue();
        replay.command.kind = OrderKind::Attack;
        replay.command.targetEntity = ecs::handle_of(target);
        replay.targetEntity = targetSubject.value();
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptSuppressAreaCommand", [vm](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjectTexts, float startX, float startY,
        float endX, float endY, float width, int shotsPerUnit) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::SuppressArea;
        replay.units = std::move(subjects).takeValue();
        replay.command.kind = OrderKind::SuppressArea;
        replay.command.target = {startX, startY};
        replay.command.secondaryTarget = {endX, endY};
        replay.command.radius = width;
        replay.priority = shotsPerUnit;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    cls.addFunc("queueScriptEscortCommand", [vm](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjectTexts, const std::string& targetText,
        float guardRadius, float spacing) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto target = parseScriptSubject(targetText, "target");
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.operation = RTSReplayOperation::Escort;
        replay.units = std::move(subjects).takeValue();
        replay.targetEntity = target.value();
        replay.command.kind = OrderKind::Escort;
        replay.command.radius = guardRadius;
        replay.formation.spacing = spacing;
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    });
    const auto queueUnitOrder = [vm](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjectTexts, OrderKind kind,
        WorldPosition point, SubjectRef target, bool append,
        float formationSpacing) -> ssq::Table {
        if (tick < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay tick must be non-negative", "tick")), false, false);
        if (formationSpacing == 0.0f || !std::isfinite(formationSpacing))
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay formation spacing must be positive", "spacing")),
                false, false);
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        RTSReplayCommand replay;
        replay.tick = SimulationTick{static_cast<std::uint64_t>(tick)};
        replay.units = std::move(subjects).takeValue();
        replay.command.kind = kind;
        replay.command.target = point;
        replay.command.append = append;
        replay.targetEntity = target;
        if (formationSpacing > 0.0f) {
            replay.formation.kind = FormationKind::Grid;
            replay.formation.spacing = formationSpacing;
        }
        return script::projectResult(vm, self->queueScriptCommand(std::move(replay)));
    };
    cls.addFunc("queueScriptStopCommand", [queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects) -> ssq::Table {
        return queueUnitOrder(self, tick, subjects, OrderKind::Stop, {}, {}, false, -1.0f);
    });
    cls.addFunc("queueScriptHoldCommand", [queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects) -> ssq::Table {
        return queueUnitOrder(self, tick, subjects, OrderKind::HoldPosition, {}, {}, false, -1.0f);
    });
    cls.addFunc("queueScriptPatrolCommand", [queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, float x, float y) -> ssq::Table {
        return queueUnitOrder(self, tick, subjects, OrderKind::Patrol, {x, y}, {}, false, -1.0f);
    });
    cls.addFunc("queueScriptAttackGroundCommand", [queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, float x, float y, bool append) -> ssq::Table {
        return queueUnitOrder(self, tick, subjects, OrderKind::AttackGround, {x, y}, {}, append, -1.0f);
    });
    cls.addFunc("queueScriptAppendMoveCommand", [vm, queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, float x, float y, float spacing) -> ssq::Table {
        if (!std::isfinite(spacing) || spacing <= 0.0f)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay formation spacing must be positive", "spacing")),
                false, false);
        return queueUnitOrder(self, tick, subjects, OrderKind::Move, {x, y}, {}, true, spacing);
    });
    cls.addFunc("queueScriptAppendAttackMoveCommand", [vm, queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, float x, float y, float spacing) -> ssq::Table {
        if (!std::isfinite(spacing) || spacing <= 0.0f)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS replay formation spacing must be positive", "spacing")),
                false, false);
        return queueUnitOrder(self, tick, subjects, OrderKind::AttackMove, {x, y}, {}, true, spacing);
    });
    const auto queueEntityOrder = [vm, queueUnitOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, const std::string& targetText,
        OrderKind kind) -> ssq::Table {
        auto target = parseScriptSubject(targetText, "target");
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return queueUnitOrder(self, tick, subjects, kind, {}, target.value(), false, -1.0f);
    };
    cls.addFunc("queueScriptRepairCommand", [queueEntityOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, const std::string& target) -> ssq::Table {
        return queueEntityOrder(self, tick, subjects, target, OrderKind::Repair);
    });
    cls.addFunc("queueScriptCaptureCommand", [queueEntityOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, const std::string& target) -> ssq::Table {
        return queueEntityOrder(self, tick, subjects, target, OrderKind::Capture);
    });
    cls.addFunc("queueScriptGarrisonCommand", [queueEntityOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, const std::string& target) -> ssq::Table {
        return queueEntityOrder(self, tick, subjects, target, OrderKind::Garrison);
    });
    cls.addFunc("queueScriptBoardTransportCommand", [queueEntityOrder](RTS* self, std::int64_t tick,
        const std::vector<std::string>& subjects, const std::string& target) -> ssq::Table {
        return queueEntityOrder(self, tick, subjects, target, OrderKind::BoardTransport);
    });
    cls.addFunc("moveUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                  float x, float y, bool append, float spacing) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::Move;
        command.target = {x, y};
        command.append = append;
        FormationSpec formation;
        formation.kind = FormationKind::Grid;
        formation.spacing = spacing;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command, formation), fanOutValue);
    });
    cls.addFunc("attackMoveUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                        float x, float y, bool append, float spacing) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::AttackMove;
        command.target = {x, y};
        command.append = append;
        FormationSpec formation;
        formation.kind = FormationKind::Grid;
        formation.spacing = spacing;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command, formation), fanOutValue);
    });
    cls.addFunc("attackUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                    const std::string& targetText, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(targetText, "target");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        ecs::Entity* target = self->findUnit(targetSubject.value());
        if (target == nullptr) target = self->findBuilding(targetSubject.value());
        if (target == nullptr)
            return script::projectStatusResult(vm,
                Status::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                  "RTS attack target identity was not found", "target")),
                false, false);
        CommandSpec command;
        command.kind = OrderKind::Attack;
        command.targetEntity = ecs::handle_of(target);
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("attackGroundUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                           float x, float y, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::AttackGround;
        command.target = {x, y};
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("suppressAreaUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
        float startX, float startY, float endX, float endY, float width,
        int shotsPerUnit) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        return script::projectResult(vm, self->suppressArea(
            std::move(subjects).takeValue(), {startX, startY}, {endX, endY}, width, shotsPerUnit),
            fanOutValue);
    });
    cls.addFunc("escortUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
        const std::string& targetText, float guardRadius, float spacing) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto target = parseScriptSubject(targetText, "target");
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return script::projectResult(vm, self->escortUnits(
            std::move(subjects).takeValue(), target.value(), guardRadius, spacing), fanOutValue);
    });
    cls.addFunc("setUnitStance", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                       const std::string& stanceText, float leashRange) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto stance = parseCombatStance(stanceText);
        if (!stance) return script::projectStatusResult(vm, stance.status(), false, false);
        return script::projectResult(vm, self->setUnitStance(
            std::move(subjects).takeValue(), stance.value(), leashRange));
    });
    cls.addFunc("setUnitMovementPriority", [vm](RTS* self,
        const std::vector<std::string>& subjectTexts, int priority) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        return script::projectResult(vm, self->setUnitMovementPriority(
            std::move(subjects).takeValue(), priority));
    });
    cls.addFunc("assignWorker", [vm](RTS* self, const std::string& workerText,
                                      const std::string& nodeText,
                                      const std::string& dropoffText) -> ssq::Table {
        auto workerSubject = parseScriptSubject(workerText, "worker");
        if (!workerSubject)
            return script::projectStatusResult(vm, workerSubject.status(), false, false);
        auto nodeSubject = parseScriptSubject(nodeText, "node");
        if (!nodeSubject)
            return script::projectStatusResult(vm, nodeSubject.status(), false, false);
        auto dropoffSubject = parseScriptSubject(dropoffText, "dropoff");
        if (!dropoffSubject)
            return script::projectStatusResult(vm, dropoffSubject.status(), false, false);
        auto* worker = self->findUnit(workerSubject.value());
        auto* node = self->findResourceNode(nodeSubject.value());
        auto* dropoff = self->findBuilding(dropoffSubject.value());
        if (worker == nullptr || node == nullptr || dropoff == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS worker assignment root was not found", "assignment")),
                false, false);
        return script::projectResult(vm, self->assignWorker(*worker, *node, *dropoff));
    });
    cls.addFunc("setWorkerAutoAssignment", [vm](RTS* self,
        const std::vector<std::string>& subjectTexts, bool enabled) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        return script::projectResult(vm, self->setWorkerAutoAssignment(
            std::move(subjects).takeValue(), enabled));
    });
    cls.addFunc("configureAutoConstruction", [vm](RTS* self, const std::string& factionText,
        bool enabled, int maxBuildersPerSite, int reserveWorkers) -> ssq::Table {
        auto subject = parseScriptSubject(factionText, "faction");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* faction = self->findFaction(subject.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS workforce faction was not found", "faction")), false, false);
        const auto workforce = faction->workforce();
        return script::projectResult(vm, self->configureWorkforce(*faction, enabled,
            maxBuildersPerSite, workforce->autoRepair,
            static_cast<int>(workforce->maxRepairersPerBuilding), reserveWorkers));
    });
    cls.addFunc("configureAutoRepair", [vm](RTS* self, const std::string& factionText,
        bool enabled, int maxRepairersPerBuilding, int reserveWorkers) -> ssq::Table {
        auto subject = parseScriptSubject(factionText, "faction");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* faction = self->findFaction(subject.value());
        if (faction == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS workforce faction was not found", "faction")), false, false);
        const auto workforce = faction->workforce();
        return script::projectResult(vm, self->configureWorkforce(*faction,
            workforce->autoConstruction, static_cast<int>(workforce->maxBuildersPerSite),
            enabled, maxRepairersPerBuilding, reserveWorkers));
    });
    cls.addFunc("assignBuilder", [vm](RTS* self, const std::string& workerText,
                                        const std::string& buildingText) -> ssq::Table {
        auto workerSubject = parseScriptSubject(workerText, "worker");
        if (!workerSubject)
            return script::projectStatusResult(vm, workerSubject.status(), false, false);
        auto buildingSubject = parseScriptSubject(buildingText, "building");
        if (!buildingSubject)
            return script::projectStatusResult(vm, buildingSubject.status(), false, false);
        auto* worker = self->findUnit(workerSubject.value());
        auto* building = self->findBuilding(buildingSubject.value());
        if (worker == nullptr || building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS builder assignment root was not found", "assignment")),
                false, false);
        return script::projectResult(vm, self->assignBuilder(*worker, *building),
                                     [](std::string id) { return Value(std::move(id)); });
    });
    cls.addFunc("addUnitReserveAmmo", [vm](RTS* self, const std::string& subjectText,
                                             int rounds) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "unit");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* unit = self->findUnit(subject.value());
        if (unit == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS reserve-ammunition unit was not found", "unit")), false, false);
        return script::projectResult(vm, self->addUnitReserveAmmo(*unit, rounds),
                                     [](int value) { return Value(static_cast<std::int64_t>(value)); });
    });
    cls.addFunc("addUnitAmmoSupply", [vm](RTS* self, const std::string& subjectText,
                                            float rounds) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "unit");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* unit = self->findUnit(subject.value());
        if (unit == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS ammunition-supply unit was not found", "unit")), false, false);
        return script::projectResult(vm, self->addUnitAmmoSupply(*unit, rounds),
                                     [](float value) { return Value(static_cast<double>(value)); });
    });
    cls.addFunc("addBuildingAmmoSupply", [vm](RTS* self, const std::string& subjectText,
                                                float rounds) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "building");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* building = self->findBuilding(subject.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS ammunition-supply building was not found", "building")), false, false);
        return script::projectResult(vm, self->addBuildingAmmoSupply(*building, rounds),
                                     [](float value) { return Value(static_cast<double>(value)); });
    });
    cls.addFunc("setUnitAutoResupply", [vm](RTS* self, const std::string& subjectText,
                                              bool enabled) -> ssq::Table {
        auto subject = parseScriptSubject(subjectText, "unit");
        if (!subject) return script::projectStatusResult(vm, subject.status(), false, false);
        auto* unit = self->findUnit(subject.value());
        if (unit == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS automatic-resupply unit was not found", "unit")), false, false);
        return script::projectResult(vm, self->setUnitAutoResupply(*unit, enabled));
    });
    cls.addFunc("heal", [vm](RTS* self, const std::string& sourceText,
                               const std::string& targetText, double amount) -> ssq::Table {
        auto source = parseScriptSubject(sourceText, "source");
        if (!source) return script::projectStatusResult(vm, source.status(), false, false);
        auto target = parseScriptSubject(targetText, "target");
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return script::projectResult(vm, self->heal(source.value(), target.value(), amount),
                                     [](double applied) { return Value(applied); });
    });
    cls.addFunc("applyStatusEffect", [vm](RTS* self, const std::string& sourceText,
        const std::string& targetText, const std::string& effect, double durationOverride) -> ssq::Table {
        auto source = parseScriptSubject(sourceText, "source");
        if (!source) return script::projectStatusResult(vm, source.status(), false, false);
        auto target = parseScriptSubject(targetText, "target");
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return script::projectResult(vm,
            self->applyStatusEffect(source.value(), target.value(), effect, durationOverride),
            [](effects::EffectHandle handle) {
                return Value(Value::Object{{"instanceId", std::move(handle.instanceId)},
                    {"generation", static_cast<std::int64_t>(handle.containerGeneration)}});
            });
    });
    cls.addFunc("requestFireSupport", [vm](RTS* self, const std::string& requesterText,
        float x, float y, float radius, int shotsPerResponder, int maxResponders) -> ssq::Table {
        if (maxResponders < 0)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "RTS fire-support responder limit must be non-negative", "maxResponders")), false, false);
        auto requesterSubject = parseScriptSubject(requesterText, "requester");
        if (!requesterSubject)
            return script::projectStatusResult(vm, requesterSubject.status(), false, false);
        auto* requester = self->findUnit(requesterSubject.value());
        if (requester == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS fire-support requester was not found", "requester")), false, false);
        return script::projectResult(vm, self->requestFireSupport(
            *requester, {x, y}, radius, shotsPerResponder, static_cast<std::size_t>(maxResponders)),
            [](std::size_t count) { return Value(static_cast<std::int64_t>(count)); });
    });
    cls.addFunc("cancelFireSupport", [vm](RTS* self,
        const std::string& requesterText) -> ssq::Table {
        auto requesterSubject = parseScriptSubject(requesterText, "requester");
        if (!requesterSubject)
            return script::projectStatusResult(vm, requesterSubject.status(), false, false);
        auto* requester = self->findUnit(requesterSubject.value());
        if (requester == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS fire-support requester was not found", "requester")), false, false);
        return script::projectResult(vm, self->cancelFireSupport(*requester),
            [](std::size_t count) { return Value(static_cast<std::int64_t>(count)); });
    });
    cls.addFunc("patrolUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                     float x, float y, float spacing) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::Patrol;
        command.target = {x, y};
        FormationSpec formation;
        formation.kind = FormationKind::Grid;
        formation.spacing = spacing;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command, formation), fanOutValue);
    });
    cls.addFunc("repairUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                     const std::string& buildingText, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(buildingText, "building");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        Building* building = self->findBuilding(targetSubject.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS repair building identity was not found", "building")), false, false);
        CommandSpec command;
        command.kind = OrderKind::Repair;
        command.targetEntity = ecs::handle_of(building);
        command.target = {building->placement()->worldX, building->placement()->worldY};
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("captureUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                      const std::string& buildingText, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(buildingText, "building");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        Building* building = self->findBuilding(targetSubject.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS capture building identity was not found", "building")), false, false);
        CommandSpec command;
        command.kind = OrderKind::Capture;
        command.targetEntity = ecs::handle_of(building);
        command.target = {building->placement()->worldX, building->placement()->worldY};
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("garrisonUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                       const std::string& buildingText, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(buildingText, "building");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        Building* building = self->findBuilding(targetSubject.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS garrison building identity was not found", "building")), false, false);
        CommandSpec command;
        command.kind = OrderKind::Garrison;
        command.targetEntity = ecs::handle_of(building);
        command.target = {building->placement()->worldX, building->placement()->worldY};
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("boardTransportUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts,
                                             const std::string& transportText, bool append) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        auto targetSubject = parseScriptSubject(transportText, "transport");
        if (!targetSubject) return script::projectStatusResult(vm, targetSubject.status(), false, false);
        Unit* transport = self->findUnit(targetSubject.value());
        if (transport == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS transport identity was not found", "transport")), false, false);
        CommandSpec command;
        command.kind = OrderKind::BoardTransport;
        command.targetEntity = ecs::handle_of(transport);
        command.target = {transport->motion()->x, transport->motion()->y};
        command.append = append;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("unloadTransport", [vm](RTS* self, const std::string& transportText,
                                         float x, float y) -> ssq::Table {
        auto subjectValue = parseScriptSubject(transportText, "transport");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Unit* transport = self->findUnit(subjectValue.value());
        if (transport == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS transport identity was not found", "transport")), false, false);
        return script::projectResult(vm, self->unloadTransport(*transport, {x, y}),
            [](std::size_t released) { return Value(static_cast<std::int64_t>(released)); });
    });
    cls.addFunc("evacuateBuilding", [vm](RTS* self, const std::string& buildingText,
                                          float x, float y) -> ssq::Table {
        auto subjectValue = parseScriptSubject(buildingText, "building");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Building* building = self->findBuilding(subjectValue.value());
        if (building == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS building identity was not found", "building")), false, false);
        return script::projectResult(vm, self->evacuateBuilding(*building, {x, y}),
            [](std::size_t released) { return Value(static_cast<std::int64_t>(released)); });
    });
    cls.addFunc("setUnitCloaked", [vm](RTS* self, const std::string& unitText, bool cloaked) -> ssq::Table {
        auto subjectValue = parseScriptSubject(unitText, "unit");
        if (!subjectValue) return script::projectStatusResult(vm, subjectValue.status(), false, false);
        Unit* unit = self->findUnit(subjectValue.value());
        if (unit == nullptr)
            return script::projectStatusResult(vm, Status::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "RTS unit identity was not found", "unit")), false, false);
        return script::projectResult(vm, self->setUnitCloaked(*unit, cloaked));
    });
    cls.addFunc("stopUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::Stop;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
    cls.addFunc("holdUnits", [vm](RTS* self, const std::vector<std::string>& subjectTexts) -> ssq::Table {
        auto subjects = parseScriptSubjects(subjectTexts);
        if (!subjects) return script::projectStatusResult(vm, subjects.status(), false, false);
        CommandSpec command;
        command.kind = OrderKind::HoldPosition;
        return script::projectResult(vm,
            self->commandUnits(std::move(subjects).takeValue(), command), fanOutValue);
    });
}

}  // namespace eve::rts
