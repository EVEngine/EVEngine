#include <cmath>
#include "crowd/Crowd.h"
#include "rts/RTS.h"
#include "weapon/Weapon.h"
#include "weapon/WeaponDefinitionRuntime.h"

namespace eve::rts {
struct PreparedUnitBatch::Impl {
    RTS*                        owner = nullptr;  // @lifetime Guarded by ownerLifetime on the simulation owner thread.
    LifetimeWitness             ownerLifetime;
    std::shared_ptr<ecs::Table> world;
    ecs::EntityHandle           faction;
    crowd::Crowd*   crowd = nullptr;  // @lifetime Guarded by crowdLifetime; binding checked again at commit.
    LifetimeWitness crowdLifetime;
    std::vector<UnitBatchRequest>                               requests;
    std::vector<std::optional<weapon::WeaponDefinitionRuntime>> weapons;
};
PreparedUnitBatch::PreparedUnitBatch(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedUnitBatch::~PreparedUnitBatch()                                       = default;
PreparedUnitBatch::PreparedUnitBatch(PreparedUnitBatch&&) noexcept            = default;
PreparedUnitBatch& PreparedUnitBatch::operator=(PreparedUnitBatch&&) noexcept = default;

Result<PreparedUnitBatch> RTS::prepareUnitBatch(Faction& faction, std::span<const UnitBatchRequest> requests) {
    if (!ownedWorld_ || requests.empty() || requests.size() > 1024)
        return Result<PreparedUnitBatch>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Batch requires an owning table and 1..1024 units", "rts.spawn"));
    if (ownedWorld_->is_deferred())
        return Result<PreparedUnitBatch>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Unit batch requires a domain outside Views", "rts.spawn"));
    auto handle = ecs::handle_of(&faction);
    if (handle.table != ownedWorld_.get())
        return Result<PreparedUnitBatch>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Faction belongs to another table", "rts.spawn.faction"));
    ecs::ScopedTable scope(*ownedWorld_);
    if (findFaction(faction.identity()->subject) != &faction)
        return Result<PreparedUnitBatch>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Faction is not owned by this facade", "rts.spawn.faction"));
    auto provider = checkCrowdLifetime();
    if (!provider) return Result<PreparedUnitBatch>::failure(provider.status());
    if (!crowd_)
        return Result<PreparedUnitBatch>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Unit batch requires a Crowd provider", "rts.spawn.crowd"));
    auto impl           = std::make_unique<PreparedUnitBatch::Impl>();
    impl->owner         = this;
    impl->ownerLifetime = lifetimeWitness();
    impl->world         = ownedWorld_;
    impl->faction       = handle;
    impl->crowd         = crowd_;
    impl->crowdLifetime = crowdLifetime_;
    impl->requests.assign(requests.begin(), requests.end());
    impl->weapons.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto& request = requests[index];
        if (request.initialCommand) {
            auto valid = request.initialCommand->validate();
            if (!valid) return Result<PreparedUnitBatch>::failure(valid.status());
            const auto target = request.initialCommand->targetEntity;
            if (target.table && (target.table != ownedWorld_.get() || !ecs::try_get(target)))
                return Result<PreparedUnitBatch>::failure(Diagnostic::error(
                    DiagnosticCode::StaleHandle, "Initial command target is stale or belongs to another table",
                    "rts.spawn.command"));
            impl->requests[index].initialCommand = std::make_shared<const CommandSpec>(*request.initialCommand);
        }
        if (!request.subject.isValid() || !request.recipe.source() || !std::isfinite(request.x) ||
            !std::isfinite(request.y))
            return Result<PreparedUnitBatch>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid unit batch request", "rts.spawn.unit"));
        if (ownsSubject(request.subject))
            return Result<PreparedUnitBatch>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Unit subject already belongs to RTS", "rts.spawn.subject"));
        for (std::size_t prior = 0; prior < index; ++prior)
            if (requests[prior].subject == request.subject)
                return Result<PreparedUnitBatch>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Duplicate unit batch subject", "rts.spawn.subject"));
        impl->weapons.emplace_back();
        if (const auto source = request.recipe.weaponSource()) {
            auto runtime =
                weapon::WeaponDefinitionRuntime::prepareCaptured(source->get(), request.subject.persistentId());
            if (!runtime) return Result<PreparedUnitBatch>::failure(runtime.status());
            impl->weapons.back().emplace(std::move(runtime).takeValue());
        }
    }
    return Result<PreparedUnitBatch>::success(PreparedUnitBatch(std::move(impl)));
}

Result<UnitBatchReceipt> PreparedUnitBatch::commit() {
    auto impl = std::move(impl_);
    if (!impl || !impl->ownerLifetime.isAlive() || !impl->crowdLifetime.isAlive())
        return Result<UnitBatchReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Unit batch owner or provider expired", "rts.spawn"));
    auto& owner = *impl->owner;
    if (owner.crowd_ != impl->crowd || owner.ownedWorld_ != impl->world)
        return Result<UnitBatchReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Unit batch binding changed", "rts.spawn"));
    if (impl->world->is_deferred())
        return Result<UnitBatchReceipt>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Unit batch requires a domain outside Views", "rts.spawn"));
    ecs::ScopedTable scope(*impl->world);
    auto*            faction = dynamic_cast<Faction*>(ecs::try_get(impl->faction));
    if (!faction || owner.findFaction(faction->identity()->subject) != faction)
        return Result<UnitBatchReceipt>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Unit batch faction expired", "rts.spawn.faction"));
    for (const auto& request : impl->requests) {
        if (request.initialCommand && request.initialCommand->targetEntity.table &&
            !ecs::try_get(request.initialCommand->targetEntity))
            return Result<UnitBatchReceipt>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "Initial command target expired before commit", "rts.spawn.command"));
        if (owner.ownsSubject(request.subject))
            return Result<UnitBatchReceipt>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "Prepared unit subject is now occupied", "rts.spawn.subject"));
    }
    auto factionLink = FactionLink::bind(impl->faction);
    if (!factionLink) return Result<UnitBatchReceipt>::failure(factionLink.status());
    auto&      members         = faction->members()->units;
    const auto originalMembers = members.size(), originalUnits = owner.units_.size(),
               originalWeapons = owner.weapons_.size();
    // Arm rollback before any entity creation or lazy component access. No callbacks are invoked while roots are
    // tentative.
    const auto rollback = [&](RTS*) noexcept {
        members.resize(originalMembers);
        while (owner.weapons_.size() > originalWeapons) {
            if (auto* entity = dynamic_cast<weapon::WeaponEntity*>(ecs::try_get(owner.weapons_.back())))
                entity->release();
            owner.weapons_.pop_back();
        }
        while (owner.units_.size() > originalUnits) {
            if (auto* entity = dynamic_cast<Unit*>(ecs::try_get(owner.units_.back()))) entity->release();
            owner.units_.pop_back();
        }
    };
    std::unique_ptr<RTS, decltype(rollback)> unpublished(&owner, rollback);
    UnitBatchReceipt                         receipt;
    receipt.subjects.reserve(impl->requests.size());
    crowd::AgentCreateBatch batch;
    batch.policy = crowd::SpawnPolicy::RejectOverlap;
    batch.agents.reserve(impl->requests.size());
    struct Projection {
        Unit::Motion* motion;
        Unit::Crowd*  crowd;
        std::string   key;
    };
    std::vector<Projection> projections;
    projections.reserve(impl->requests.size());
    owner.units_.reserve(originalUnits + impl->requests.size());
    owner.weapons_.reserve(originalWeapons + impl->requests.size());
    members.reserve(originalMembers + impl->requests.size());
    for (std::size_t index = 0; index < impl->requests.size(); ++index) {
        const auto& request = impl->requests[index];
        const auto& source  = request.recipe.source()->get();
        const auto  logical = LogicalId::parse(source.type + ":" + source.id);
        if (!logical)
            return Result<UnitBatchReceipt>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Recipe has no canonical logical identity", "rts.spawn.recipe"));
        auto created = owner.newUnit(request.subject, *logical);
        if (!created) return Result<UnitBatchReceipt>::failure(created.status());
        auto*                          unit    = std::move(created).takeValue();
        CapturedArchetypeWeaponFactory factory = [&](const definitions::Definition&, PersistentId) {
            return owner.publishPreparedWeapon(*impl->weapons[index]);
        };
        auto applied = request.recipe.apply(*unit, factory);
        if (!applied) return Result<UnitBatchReceipt>::failure(applied.status());
        if (request.initialCommand) {
            auto queued = unit->orders()->values.enqueue(*request.initialCommand);
            if (!queued) return Result<UnitBatchReceipt>::failure(queued.status());
        }
        unit->faction()->link = factionLink.value();
        members.push_back(ecs::handle_of(unit));
        auto key  = request.subject.format();
        auto link = CrowdLink::bind(key);
        if (!link) return Result<UnitBatchReceipt>::failure(link.status());
        unit->crowd()->link = std::move(link).takeValue();
        auto sensing        = SensingLink::bind(key);
        if (!sensing) return Result<UnitBatchReceipt>::failure(sensing.status());
        unit->sensing()->link = std::move(sensing).takeValue();
        auto* motion          = &*unit->motion();
        auto* settings        = &*unit->crowd();
        projections.push_back({motion, settings, key});
        batch.agents.push_back({std::move(key), request.x, request.y, settings->heading, settings->radius, {}});
        receipt.subjects.push_back(request.subject);
    }
    // Last fallible publication. Everything after this point uses pre-materialized components and moved strings.
    auto placed = impl->crowd->createAgents(batch);
    if (!placed) {
        const auto* diagnostic = placed.status().primaryDiagnostic();
        if (diagnostic && (diagnostic->code() == DiagnosticCode::Conflict ||
                           diagnostic->code() == DiagnosticCode::PreconditionViolation)) {
            UnitBatchReceipt blocked;
            blocked.placementStatus = placed.status();
            return Result<UnitBatchReceipt>::success(std::move(blocked));
        }
        return Result<UnitBatchReceipt>::failure(placed.status());
    }
    auto placement = std::move(placed).takeValue();
    for (std::size_t index = 0; index < projections.size(); ++index) {
        auto&       projection              = projections[index];
        const auto& item                    = placement.created[index];
        projection.motion->x                = item.x;
        projection.motion->y                = item.y;
        projection.crowd->runtimeAgent      = item.reference.handle;
        projection.crowd->runtimeOwner      = item.reference.owner;
        projection.crowd->runtimeBindingKey = std::move(projection.key);
    }
    receipt.state = UnitBatchState::Committed;
    unpublished.release();
    return Result<UnitBatchReceipt>::success(std::move(receipt));
}
}  // namespace eve::rts
