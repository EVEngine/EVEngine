#include <algorithm>
#include <new>
#include <set>
#include <tuple>
#include "common/Profile.h"
#include "crowd/Crowd.h"
#include "rts/RTS.h"
#include "rts/RTSComponentAccessInternal.h"
#include "sensing/Sensing.h"

namespace eve::rts {
Result<void> RTS::removeUnitBatch(std::span<const ecs::EntityHandle> units) {
    EV_PROFILE_MODULE("rts", "RTS::removeUnitBatch");
    // Scope: this facade's owned Unit handles, outside Views. Read only existing components;
    // no absent reference component is materialized. Crowd is the last fallible publication.
    using detail::existingComponent;
    try {
        using Key                    = std::tuple<ecs::Table*, std::type_index, std::uint32_t, std::uint32_t>;
        const auto               key = [](ecs::EntityHandle h) { return Key{h.table, h.type, h.id, h.generation}; };
        std::set<Key>            dead, weapons;
        crowd::AgentRemovalBatch agents;
        agents.agents.reserve(units.size());
        // @lifetime Local borrows used synchronously; each root is released only after all reference sweeps.
        std::vector<std::pair<Unit*, std::string>> roots;
        roots.reserve(units.size());
        for (auto handle : units) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (!unit)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "Casualty batch requires current units", "cleanup.units"));
            const auto* containment = existingComponent<Unit::Containment>(*unit);
            if (containment && !containment->occupants.empty())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "Casualty batch requires units with no occupants", "cleanup.units"));
            dead.insert(key(handle));
            if (const auto* weapon = existingComponent<Unit::Weapon>(*unit)) weapons.insert(key(weapon->link.handle()));
            const auto* settings = existingComponent<Unit::Crowd>(*unit);
            if (crowd_ && crowdLifetime_.isAlive() && settings && settings->link.isBound())
                agents.agents.push_back({settings->runtimeAgent, settings->runtimeOwner});
            roots.emplace_back(unit, unit->identity()->subject.format());
        }
        if (!agents.agents.empty()) {
            auto removed = crowd_->removeAgents(agents);
            if (!removed) return Result<void>::failure(removed.status());
        }
        const auto removed = [&](ecs::EntityHandle h) { return dead.contains(key(h)); };
        for (auto h : resourceNodes_)
            if (auto* node = dynamic_cast<ResourceNode*>(ecs::try_get(h)))
                if (auto* harvest = existingComponent<ResourceNode::Harvest>(*node))
                    std::erase_if(harvest->workers, removed);
        for (auto h : buildings_)
            if (auto* building = dynamic_cast<Building*>(ecs::try_get(h))) {
                if (auto* construction = existingComponent<Building::Construction>(*building))
                    std::erase_if(construction->builders, removed);
                auto* garrison = existingComponent<Building::Garrison>(*building);
                if (garrison) std::erase_if(garrison->occupants, removed);
                if (auto* rally = existingComponent<Building::Rally>(*building)) {
                    std::erase_if(rally->reinforcements, removed);
                    if (removed(rally->transport)) {
                        rally->transport       = {};
                        rally->transportActive = false;
                    }
                }
                if (auto* capture = existingComponent<Building::Capture>(*building))
                    capture->blockedByGarrison = garrison && !garrison->occupants.empty();
            }
        for (auto h : units_)
            if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(h))) {
                if (auto* containment = existingComponent<Unit::Containment>(*unit)) {
                    std::erase_if(containment->occupants, removed);
                    if (containment->container.isBound() && removed(containment->container.handle()))
                        containment->container = {};
                }
                if (auto* combat = existingComponent<Unit::Combat>(*unit); combat && removed(combat->target))
                    combat->target = {};
                if (auto* tactics = existingComponent<Unit::Tactics>(*unit); tactics && removed(tactics->escortTarget))
                    tactics->escortTarget = {};
                if (auto* supply = existingComponent<Unit::Supply>(*unit)) {
                    if (removed(supply->assignedTarget)) supply->assignedTarget = {};
                    if (removed(supply->convoyLeader)) supply->convoyLeader = {};
                }
            }
        for (auto h : factions_)
            if (auto* faction = dynamic_cast<Faction*>(ecs::try_get(h)))
                if (auto* members = existingComponent<Faction::Members>(*faction))
                    std::erase_if(members->units, removed);
        for (auto h : players_)
            if (auto* player = dynamic_cast<Player*>(ecs::try_get(h)))
                if (auto* selection = existingComponent<Player::Selection>(*player))
                    std::erase_if(selection->units, removed);
        std::erase_if(weapons_, [&](auto h) { return weapons.contains(key(h)); });
        std::erase_if(units_, removed);
        for (auto& [unit, subject] : roots) {
            if (sensing_ && sensingLifetime_.isAlive() && sensing_->subjects().contains(subject))
                sensing_->remove(subject).expect("Validated synchronous sensing removal");
            combatState_.mirroredSubjects.erase(subject);
            combatState_.blockedSubjects.erase(subject);
            releaseUnreferencedUnit(*unit);
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Cannot prepare casualty cleanup storage", "cleanup.units"));
    }
}
}  // namespace eve::rts

namespace eve::rts {
struct PreparedUnitRemoval::Impl {
    RTS*            owner = nullptr;  // @lifetime Owner-thread borrow guarded by lifetime; never used after expiration.
    LifetimeWitness lifetime;
    std::shared_ptr<ecs::Table>    world;
    std::vector<SubjectRef>        subjects;
    std::vector<ecs::EntityHandle> handles;
};
PreparedUnitRemoval::PreparedUnitRemoval(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedUnitRemoval::~PreparedUnitRemoval()                                                = default;
PreparedUnitRemoval::PreparedUnitRemoval(PreparedUnitRemoval&&) noexcept                   = default;
PreparedUnitRemoval&        PreparedUnitRemoval::operator=(PreparedUnitRemoval&&) noexcept = default;
Result<PreparedUnitRemoval> RTS::prepareUnitRemoval(std::span<const SubjectRef> subjects) {
    if (!ownedWorld_ || subjects.size() > 1024)
        return Result<PreparedUnitRemoval>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Removal requires an owning table and at most 1024 identities", "rts.spawn.remove"));
    if (ownedWorld_->is_deferred())
        return Result<PreparedUnitRemoval>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Removal requires a domain outside Views", "rts.spawn.remove"));
    ecs::ScopedTable scope(*ownedWorld_);
    auto             impl = std::make_unique<PreparedUnitRemoval::Impl>();
    impl->owner           = this;
    impl->lifetime        = lifetimeWitness();
    impl->world           = ownedWorld_;
    impl->subjects.assign(subjects.begin(), subjects.end());
    impl->handles.reserve(subjects.size());
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        if (!subjects[index].isValid())
            return Result<PreparedUnitRemoval>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid removal identity", "rts.spawn.remove"));
        for (std::size_t prior = 0; prior < index; ++prior)
            if (subjects[prior] == subjects[index])
                return Result<PreparedUnitRemoval>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "Duplicate removal identity", "rts.spawn.remove"));
        auto* unit = findUnit(subjects[index]);
        if (unit && ecs::handle_of(unit).table != ownedWorld_.get())
            return Result<PreparedUnitRemoval>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "Removal target belongs to another table", "rts.spawn.remove"));
        impl->handles.push_back(unit ? ecs::handle_of(unit) : ecs::EntityHandle{});
    }
    return Result<PreparedUnitRemoval>::success(PreparedUnitRemoval(std::move(impl)));
}
Result<std::size_t> PreparedUnitRemoval::commit() {
    auto impl = std::move(impl_);
    if (!impl || !impl->lifetime.isAlive())
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Removal owner expired", "rts.spawn.remove"));
    auto& owner = *impl->owner;
    if (owner.ownedWorld_ != impl->world)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Removal table binding changed", "rts.spawn.remove"));
    if (impl->world->is_deferred())
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Removal requires a domain outside Views", "rts.spawn.remove"));
    ecs::ScopedTable               scope(*impl->world);
    std::vector<ecs::EntityHandle> live;
    live.reserve(impl->handles.size());
    for (std::size_t index = 0; index < impl->subjects.size(); ++index) {
        auto* current = owner.findUnit(impl->subjects[index]);
        if (!current) continue;
        const auto now      = ecs::handle_of(current);
        const auto captured = impl->handles[index];
        if (now.table != captured.table || now.type != captured.type || now.id != captured.id ||
            now.generation != captured.generation)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "Removal target incarnation changed", "rts.spawn.remove"));
        live.push_back(now);
    }
    if (!live.empty()) {
        auto removed = owner.removeUnitBatch(live);
        if (!removed) return Result<std::size_t>::failure(removed.status());
    }
    return Result<std::size_t>::success(impl->subjects.size());
}
}  // namespace eve::rts
