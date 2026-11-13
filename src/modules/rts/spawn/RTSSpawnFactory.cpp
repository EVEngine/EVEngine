// eve-hot-path
#include "rts/spawn/RTSSpawnFactory.h"
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include "definitions/Definitions.h"
#include "rts/RTS.h"
#include "rts/RTSComponentAccessInternal.h"
#include "rts/RTSSnapshotInternal.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "rts/spawn/RTSSpawnCaptureInternal.h"
#include "weapon/WeaponCapture.h"
namespace eve::rts_spawn {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "rts.spawn"));
}
using rts::detail::existingComponent;
using Key = std::tuple<std::uint32_t, std::uint32_t>;
Key key(spawn::SpawnerHandle source) { return {source.slot.index(), source.slot.generation()}; }
enum class Admission { Fresh, Replay };
}  // namespace
struct RTSSpawnFactory::Impl {
    rts::RTS*       domain = nullptr;  // @lifetime Owner-thread borrow guarded by lifetime before every dereference.
    LifetimeWitness lifetime;
    std::weak_ptr<ecs::Table>                        world;
    ecs::EntityHandle                                faction;
    std::shared_ptr<definitions::DefinitionRegistry> registry;
    std::shared_ptr<const rts::CommandSpec>          command;
    std::shared_ptr<const RTSSpawnCapture>           restored;
    std::map<Key, std::uint64_t>                     restoredFloors;
    bool                                             sourcesBound = false;
    bool                                             ready() const noexcept { return !restored || sourcesBound; }
    Result<std::shared_ptr<const rts::PreparedUnitArchetype>> resolve(const DefinitionRef& reference) const {
        using R = Result<std::shared_ptr<const rts::PreparedUnitArchetype>>;
        if (restored) return restored->resolveFuture(reference);
        if (!registry)
            return fail<std::shared_ptr<const rts::PreparedUnitArchetype>>(DiagnosticCode::NotFound,
                                                                           "Unit content authority missing");
        auto logical = LogicalId::parse(reference.format());
        if (!logical)
            return fail<std::shared_ptr<const rts::PreparedUnitArchetype>>(DiagnosticCode::InvalidArgument,
                                                                           "Invalid unit reference");
        auto recipe = rts::PreparedUnitArchetype::prepare(*registry, *logical);
        if (!recipe) return R::failure(recipe.status());
        return R::success(std::make_shared<const rts::PreparedUnitArchetype>(std::move(recipe).takeValue()));
    }
    struct Binding {
        ecs::EntityHandle                                 unit;
        std::optional<spawn::SpawnerHandle>               origin;
        spawn::SourceLease                                lease;
        std::shared_ptr<const rts::PreparedUnitArchetype> recipe;
    };
    struct Operation {
        spawn::BatchRequest       request;
        std::vector<spawn::Point> positions;
        bool matches(const spawn::BatchRequest& other, std::span<const spawn::Point> points) const {
            if (request.source != other.source || request.operation != other.operation ||
                request.units.size() != other.units.size() || points.size() != positions.size())
                return false;
            for (std::size_t i = 0; i < points.size(); ++i)
                if (request.units[i].subject != other.units[i].subject ||
                    request.units[i].definition != other.units[i].definition || positions[i].x != points[i].x ||
                    positions[i].y != points[i].y)
                    return false;
            return true;
        }
    };
    std::map<PersistentId, Binding>     units;
    std::map<Key, Operation>            operations;
    spawn::WorldIncarnation             sourceIncarnation;
    PersistentId                        sourceOwner;
    std::uint64_t                       epoch = 0, sequence = 0;
    bool                                busy = false;
    Result<std::shared_ptr<ecs::Table>> context() const {
        auto table = world.lock();
        if (!lifetime.isAlive() || !table)
            return fail<std::shared_ptr<ecs::Table>>(DiagnosticCode::StaleHandle, "RTS owner expired");
        if (busy || table->is_deferred())
            return fail<std::shared_ptr<ecs::Table>>(DiagnosticCode::PreconditionViolation,
                                                     "Factory requires an idle domain outside Views");
        return Result<std::shared_ptr<ecs::Table>>::success(std::move(table));
    }
    Result<Admission> validate(const spawn::BatchRequest& request, std::span<const spawn::Point> positions) const {
        if (request.operation.isNil() || request.units.empty() || request.units.size() > 1024 ||
            request.units.size() != positions.size() || request.ordinal.isZero())
            return fail<Admission>(DiagnosticCode::InvalidArgument, "Invalid batch shape");
        if (!request.lifetime.isCurrent() || request.lifetime.source() != request.source)
            return fail<Admission>(DiagnosticCode::StaleHandle, "Spawn source expired or mismatched");
        auto operation = request.lifetime.operationIdentity(request.ordinal);
        if (!operation) return Result<Admission>::failure(operation.status());
        if (operation.value() != request.operation)
            return fail<Admission>(DiagnosticCode::Conflict, "Noncanonical operation identity");
        for (std::size_t i = 0; i < request.units.size(); ++i) {
            auto member = request.lifetime.unitIdentity(request.ordinal, static_cast<std::uint32_t>(i));
            if (!member) return Result<Admission>::failure(member.status());
            if (member.value() != request.units[i].subject || !std::isfinite(static_cast<float>(positions[i].x)) ||
                !std::isfinite(static_cast<float>(positions[i].y)))
                return fail<Admission>(DiagnosticCode::InvalidArgument, "Invalid member identity or native position");
        }
        if (!sourceOwner.isNil() && sourceOwner != request.source.owner)
            return fail<Admission>(DiagnosticCode::Conflict, "Factory belongs to another Spawn world");
        if (!sourceIncarnation.isZero() && sourceIncarnation != request.source.incarnation)
            return fail<Admission>(DiagnosticCode::StaleHandle, "Factory world incarnation is stale");
        if (request.source.epoch < epoch) return fail<Admission>(DiagnosticCode::StaleHandle, "Old Spawn epoch");
        if (request.source.epoch > epoch) {
            for (const auto& [id, binding] : units)
                if (binding.origin)
                    return fail<Admission>(DiagnosticCode::PreconditionViolation,
                                           "Release old origins before changing epoch");
            return Result<Admission>::success(Admission::Fresh);
        }
        auto found = operations.find(key(request.source));
        if (found != operations.end()) {
            if (request.ordinal.value() < found->second.request.ordinal.value())
                return fail<Admission>(DiagnosticCode::StaleHandle, "Operation precedes retained receipt");
            if (request.ordinal == found->second.request.ordinal) {
                if (!found->second.matches(request, positions))
                    return fail<Admission>(DiagnosticCode::Conflict, "Receipt payload mismatch");
                return Result<Admission>::success(Admission::Replay);
            }
        }
        auto floor = restoredFloors.find(key(request.source));
        if (floor != restoredFloors.end() && request.ordinal.value() <= floor->second)
            return fail<Admission>(DiagnosticCode::StaleHandle, "Operation is below restored retired watermark");
        return Result<Admission>::success(Admission::Fresh);
    }
    struct Prepared final : spawn::IPreparedSpawn {
        std::weak_ptr<Impl>                   owner;
        spawn::BatchRequest                   request;
        std::vector<spawn::Point>             positions;
        std::optional<rts::PreparedUnitBatch> native;
        std::map<PersistentId, Binding>       staged;
        std::map<Key, Operation>              receipt;
        bool                                  published = false;
        bool                                  isPublished() const noexcept override { return published; }
        Result<spawn::CommitOutcome>          commit() override {
            auto state = owner.lock();
            if (!state) return fail<spawn::CommitOutcome>(DiagnosticCode::StaleHandle, "Factory expired");
            auto table = state->context();
            if (!table) return Result<spawn::CommitOutcome>::failure(table.status());
            if (published && request.lifetime.isCurrent() && request.lifetime.source() == request.source)
                return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
            auto admission = state->validate(request, positions);
            if (!admission) return Result<spawn::CommitOutcome>::failure(admission.status());
            if (admission.value() == Admission::Replay) {
                published = true;
                return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
            }
            if (published || !native)
                return fail<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                           "Prepare a new token after a blocked/consumed attempt");
            for (const auto& unit : request.units)
                if (state->units.contains(unit.subject.persistentId()))
                    return fail<spawn::CommitOutcome>(DiagnosticCode::Conflict, "Unit identity is already tracked");
            state->busy = true;
            struct Busy {
                bool& value;
                ~Busy() { value = false; }
            } busy{state->busy};
            auto outcome = native->commit();
            native.reset();
            if (!outcome) return Result<spawn::CommitOutcome>::failure(outcome.status());
            if (outcome.value().state == rts::UnitBatchState::Blocked)
                return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::PlacementBlocked);
            ecs::ScopedTable scope(*table.value());
            // Native commit has published; all nodes below were reserved before publication.
            for (auto& [id, binding] : staged)
                binding.unit = ecs::handle_of(state->domain->findUnit(SubjectRef::fromPersistentId(id)));
            state->units.merge(staged);
            if (state->epoch != request.source.epoch) {
                state->operations.clear();
                state->restoredFloors.clear();
            } else
                state->operations.erase(key(request.source));
            state->operations.merge(receipt);
            state->epoch             = request.source.epoch;
            state->sourceIncarnation = request.source.incarnation;
            state->sourceOwner       = request.source.owner;
            published                = true;
            return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
        }
    };
};
RTSSpawnFactory::RTSSpawnFactory(rts::RTS& domain, SubjectRef faction,
                                 std::shared_ptr<definitions::DefinitionRegistry> registry,
                                 std::shared_ptr<const rts::CommandSpec>          command)
    : impl_(std::make_shared<Impl>()) {
    impl_->domain   = &domain;
    impl_->lifetime = domain.lifetimeWitness();
    impl_->world    = domain.ownedWorld();
    impl_->registry = std::move(registry);
    if (command) impl_->command = std::make_shared<const rts::CommandSpec>(*command);
    if (auto world = impl_->world.lock()) {
        ecs::ScopedTable scope(*world);
        if (auto* value = domain.findFaction(faction)) impl_->faction = ecs::handle_of(value);
    }
}
RTSSpawnFactory::~RTSSpawnFactory() = default;
Result<std::unique_ptr<RTSSpawnFactory>> RTSSpawnFactory::prepareRestored(
    rts::RTS& domain, std::shared_ptr<const RTSSpawnCapture> captured) {
    using R = Result<std::unique_ptr<RTSSpawnFactory>>;
    if (!captured || captured->saveEpoch().isNil() || !captured->faction().isValid())
        return fail<std::unique_ptr<RTSSpawnFactory>>(DiagnosticCode::InvalidArgument,
                                                      "Complete native provenance required");
    auto world = domain.ownedWorld();
    if (!world || world->is_deferred())
        return fail<std::unique_ptr<RTSSpawnFactory>>(DiagnosticCode::PreconditionViolation,
                                                      "Restore factory needs idle actual owned Table");
    ecs::ScopedTable                        scope(*world);
    std::shared_ptr<const rts::CommandSpec> command;
    if (const auto* saved = captured->initialCommand()) {
        auto value = std::make_shared<rts::CommandSpec>(*saved);
        if (saved->targetEntity.table)
            return fail<std::unique_ptr<RTSSpawnFactory>>(DiagnosticCode::Conflict,
                                                          "Captured command contains runtime target");
        if (const auto subject = captured->initialCommandTarget(); subject.isValid()) {
            auto target = domain.resolveOwnedSubject(subject);
            if (!target) return R::failure(target.status());
            value->targetEntity = std::move(target).takeValue();
        }
        command = std::move(value);
    } else if (captured->initialCommandTarget().isValid())
        return fail<std::unique_ptr<RTSSpawnFactory>>(DiagnosticCode::Conflict, "Target without initial command");
    auto result = std::make_unique<RTSSpawnFactory>(domain, captured->faction(), nullptr, std::move(command));
    result->impl_->restored = std::move(captured);
    auto valid              = result->validateDomain();
    if (!valid) return R::failure(valid.status());
    return R::success(std::move(result));
}
Result<std::vector<spawn::LifecycleInput>> RTSSpawnFactory::restoreSources(
    const spawn::SpawnWorld& core, std::span<const spawn::SavedLifecycleInput> pending,
    std::span<const rts::RTSFrameDamageEvent> damage) {
    using R = Result<std::vector<spawn::LifecycleInput>>;
    if (!impl_->restored || impl_->sourcesBound || !impl_->units.empty() || !impl_->operations.empty() ||
        !impl_->sourceOwner.isNil())
        return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::PreconditionViolation,
                                                        "Restore sources requires pristine prepared factory");
    auto valid = validateDomain();
    if (!valid) return R::failure(valid.status());
    auto world = impl_->context();
    if (!world) return R::failure(world.status());
    impl_->busy = true;
    struct Guard {
        bool& busy;
        ~Guard() { busy = false; }
    } guard{impl_->busy};
    auto projection = core.restoreBindings();
    if (!projection) return R::failure(projection.status());
    auto bindings = std::move(projection).takeValue();
    if (bindings.saveEpoch != impl_->restored->saveEpoch() || !bindings.factory ||
        &bindings.factory->domainFactory() != this)
        return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict,
                                                        "Core save epoch or actual domain factory differs");
    auto roots = impl_->domain->snapshotState();
    if (!roots) return R::failure(roots.status());
    auto relations = impl_->restored->validateRelations(bindings.membership, bindings.lifecycleSequence,
                                                        roots.value().units, pending, damage);
    if (!relations) return R::failure(relations.status());
    std::map<Key, std::uint64_t> floors;
    for (const auto& source : bindings.sources) {
        if (!source.lifetime.isCurrent() ||
            !floors.emplace(key(source.lifetime.source()), source.retiredThrough.value()).second)
            return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::StaleHandle,
                                                            "Restored source revoked or duplicated");
    }
    ecs::ScopedTable                      scope(*world.value());
    std::map<PersistentId, Impl::Binding> units;
    for (const auto& member : impl_->restored->units()) {
        if (!member.recipe || !member.recipe->source())
            return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict, "Saved member recipe missing");
        auto*             unit = impl_->domain->findUnit(member.subject);
        ecs::EntityHandle handle;
        if (unit) {
            const auto* definition = existingComponent<rts::Unit::Definition>(*unit);
            const auto* faction    = existingComponent<rts::Unit::Faction>(*unit);
            const auto& source     = member.recipe->source()->get();
            if (!definition || definition->id.namespaceName() != source.type || definition->id.name() != source.id ||
                !faction)
                return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict,
                                                                "Native member definition/faction missing or changed");
            const auto linked = faction->link.handle();
            if (linked.table != impl_->faction.table || linked.type != impl_->faction.type ||
                linked.id != impl_->faction.id || linked.generation != impl_->faction.generation)
                return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict,
                                                                "Native member belongs to another faction");
            handle = ecs::handle_of(unit);
        }
        spawn::SourceLease                  lease;
        std::optional<spawn::SpawnerHandle> origin;
        if (member.origin) {
            auto rebound = core.resolveSavedSource(spawn::savedSpawnerKey(*member.origin));
            if (!rebound) return R::failure(rebound.status());
            lease  = std::move(rebound).takeValue();
            origin = lease.source();
        }
        units.emplace(member.subject.persistentId(), Impl::Binding{handle, origin, std::move(lease), member.recipe});
    }
    std::map<Key, Impl::Operation> operations;
    for (const auto& receipt : impl_->restored->receipts()) {
        auto rebound = core.resolveSavedSource(spawn::savedSpawnerKey(receipt.source));
        if (!rebound) return R::failure(rebound.status());
        auto       lease  = std::move(rebound).takeValue();
        const auto source = lease.source();
        auto       floor  = floors.find(key(source));
        if (floor == floors.end() || receipt.ordinal.value() > floor->second)
            return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict,
                                                            "Saved receipt is ahead of admitted core history");
        spawn::BatchRequest request{receipt.operation, receipt.units, source, receipt.ordinal, lease};
        auto                admitted = impl_->validate(request, receipt.positions);
        if (!admitted) return R::failure(admitted.status());
        if (!operations.emplace(key(source), Impl::Operation{std::move(request), receipt.positions}).second)
            return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::Conflict,
                                                            "Duplicate restored latest receipt");
    }
    std::vector<spawn::LifecycleInput> remapped;
    remapped.reserve(pending.size());
    for (const auto& input : pending) {
        auto rebound = core.resolveSavedSource(input.source);
        if (!rebound) return R::failure(rebound.status());
        remapped.push_back({input.subject, input.sequence, input.reason, rebound.value().source()});
    }
    for (const auto& source : bindings.sources)
        if (!source.lifetime.isCurrent())
            return fail<std::vector<spawn::LifecycleInput>>(DiagnosticCode::StaleHandle,
                                                            "Source revoked before publication");
    impl_->units.swap(units);
    impl_->operations.swap(operations);
    impl_->restoredFloors.swap(floors);
    impl_->sequence = impl_->restored->lifecycleSequence();
    if (!bindings.sources.empty()) {
        const auto source        = bindings.sources.front().lifetime.source();
        impl_->sourceOwner       = source.owner;
        impl_->epoch             = source.epoch;
        impl_->sourceIncarnation = source.incarnation;
    }
    impl_->sourcesBound = true;
    return R::success(std::move(remapped));
}

Result<RTSSpawnCapture> RTSSpawnFactory::capture(PersistentId                   saveEpoch,
                                                 std::span<const DefinitionRef> futureUnits) const {
    using R = Result<RTSSpawnCapture>;
    if (saveEpoch.isNil()) return fail<RTSSpawnCapture>(DiagnosticCode::InvalidArgument, "Nonnil save epoch required");
    if (!impl_->ready())
        return fail<RTSSpawnCapture>(DiagnosticCode::PreconditionViolation, "Restore source binding incomplete");
    auto domain = validateDomain();
    if (!domain) return R::failure(domain.status());
    auto table = impl_->context();
    if (!table) return R::failure(table.status());
    impl_->busy = true;
    struct Guard {
        bool& busy;
        ~Guard() { busy = false; }
    } guard{impl_->busy};
    ecs::ScopedTable scope(*table.value());
    auto             result = std::make_unique<RTSSpawnCapture::Impl>();
    result->saveEpoch       = saveEpoch;
    result->owner           = impl_->sourceOwner;
    result->incarnation     = impl_->sourceIncarnation;
    result->epoch           = impl_->epoch;
    result->sequence        = impl_->sequence;
    result->faction         = dynamic_cast<rts::Faction*>(ecs::try_get(impl_->faction))->identity()->subject;
    if (impl_->command) {
        result->command   = std::make_unique<rts::CommandSpec>(*impl_->command);
        const auto target = result->command->targetEntity;
        if (target.table) {
            result->commandTarget = rts::snapshot_internal::subjectOf(target);
            if (!result->commandTarget.isValid())
                return fail<RTSSpawnCapture>(DiagnosticCode::Unsupported, "Command target has no stable RTS identity");
        }
        result->command->targetEntity = {};
    }
    result->units.reserve(impl_->units.size());
    auto pools = weapon::WeaponPoolBindings::capture(*table.value(), saveEpoch);
    if (!pools) return R::failure(pools.status());
    for (const auto& [id, binding] : impl_->units) {
        if (!binding.recipe || !binding.recipe->source())
            return fail<RTSSpawnCapture>(DiagnosticCode::InvariantViolation, "Tracked unit has no exact recipe");
        if (binding.origin && (!binding.lease.isCurrent() || binding.lease.source() != *binding.origin))
            return fail<RTSSpawnCapture>(DiagnosticCode::StaleHandle, "Tracked source lease is expired or mismatched");
        const auto  subject  = SubjectRef::fromPersistentId(id);
        auto*       unit     = dynamic_cast<rts::Unit*>(ecs::try_get(binding.unit));
        const auto* identity = unit ? existingComponent<rts::Unit::Identity>(*unit) : nullptr;
        if (unit && !identity)
            return fail<RTSSpawnCapture>(DiagnosticCode::NotFound, "Tracked unit identity is missing");
        auto* current = impl_->domain->findUnit(subject);
        if (unit != current || (identity && identity->subject != subject))
            return fail<RTSSpawnCapture>(DiagnosticCode::StaleHandle,
                                         "Tracked unit was replaced or left its owning facade");
        std::shared_ptr<const weapon::WeaponCapture> capturedWeapon;
        if (unit) {
            const auto* linked = existingComponent<rts::Unit::Weapon>(*unit);
            if (!linked) return fail<RTSSpawnCapture>(DiagnosticCode::NotFound, "Tracked unit weapon link is missing");
            if (linked->link.isBound()) {
                auto* gun = dynamic_cast<weapon::WeaponEntity*>(linked->link.resolve());
                if (!gun) return fail<RTSSpawnCapture>(DiagnosticCode::StaleHandle, "Tracked unit weapon has retired");
                const auto* state = existingComponent<weapon::WeaponEntity::State>(*gun);
                if (!state) return fail<RTSSpawnCapture>(DiagnosticCode::NotFound, "Tracked weapon state is missing");
                std::vector<weapon::WeaponCaptureTarget> targets;
                if (const auto target = state->lastRequest.targetHandle; target.table) {
                    const auto stable = rts::snapshot_internal::subjectOf(target);
                    if (!stable.isValid())
                        return fail<RTSSpawnCapture>(DiagnosticCode::Unsupported,
                                                     "Weapon target has no stable RTS identity");
                    targets.push_back({target, stable});
                }
                auto frozen = weapon::WeaponCapture::capture(*gun, saveEpoch, targets, pools.value());
                if (!frozen) return R::failure(frozen.status());
                capturedWeapon = std::make_shared<const weapon::WeaponCapture>(std::move(frozen).takeValue());
            }
        }
        result->units.push_back({subject, binding.origin,
                                 unit ? CapturedUnitPresence::Present : CapturedUnitPresence::Absent, binding.recipe,
                                 std::move(capturedWeapon)});
    }
    result->receipts.reserve(impl_->operations.size());
    for (const auto& [source, operation] : impl_->operations) {
        const auto& request = operation.request;
        if (!request.lifetime.isCurrent()) continue;
        result->receipts.push_back(
            {request.source, request.operation, request.ordinal, request.units, operation.positions});
    }
    std::map<std::string, DefinitionRef> references;
    for (const auto& reference : futureUnits) references.emplace(reference.format(), reference);
    result->future.reserve(references.size());
    for (const auto& [text, reference] : references) {
        auto recipe = impl_->resolve(reference);
        if (!recipe) return R::failure(recipe.status());
        result->future.push_back({reference, std::move(recipe).takeValue()});
    }
    return R::success(RTSSpawnCapture(std::move(result)));
}
Result<void> RTSSpawnFactory::validateDomain() const {
    auto world = impl_->context();
    if (!world) return Result<void>::failure(world.status());
    ecs::ScopedTable scope(*world.value());
    auto*            faction  = dynamic_cast<rts::Faction*>(ecs::try_get(impl_->faction));
    const auto*      identity = faction ? existingComponent<rts::Faction::Identity>(*faction) : nullptr;
    if (!identity || impl_->domain->findFaction(identity->subject) != faction)
        return fail<void>(DiagnosticCode::StaleHandle, "Factory faction expired");
    if (!impl_->domain->hasLiveCrowdProvider())
        return fail<void>(DiagnosticCode::Unsupported, "Live Crowd provider required");
    if (impl_->command) {
        auto valid = impl_->command->validate();
        if (!valid) return valid;
        const auto target = impl_->command->targetEntity;
        if (target.table && (target.table != world.value().get() || !ecs::try_get(target)))
            return fail<void>(DiagnosticCode::StaleHandle, "Initial command target expired");
    }
    return Result<void>::success();
}
Result<void> RTSSpawnFactory::validate(const DefinitionRef& reference) const {
    auto recipe = impl_->resolve(reference);
    if (!recipe) return Result<void>::failure(recipe.status());
    return Result<void>::success();
}
Result<spawn::PrepareOutcome> RTSSpawnFactory::prepare(const spawn::BatchRequest&    request,
                                                       std::span<const spawn::Point> positions) {
    if (!impl_->ready())
        return fail<spawn::PrepareOutcome>(DiagnosticCode::PreconditionViolation, "Restore source binding incomplete");
    auto table = impl_->context();
    if (!table) return Result<spawn::PrepareOutcome>::failure(table.status());
    auto admission = impl_->validate(request, positions);
    if (!admission) return Result<spawn::PrepareOutcome>::failure(admission.status());
    auto token     = std::make_unique<Impl::Prepared>();
    token->owner   = impl_;
    token->request = request;
    token->positions.assign(positions.begin(), positions.end());
    token->published = admission.value() == Admission::Replay;
    if (admission.value() != Admission::Replay) {
        ecs::ScopedTable scope(*table.value());
        auto*            faction = dynamic_cast<rts::Faction*>(ecs::try_get(impl_->faction));
        if (!faction) return fail<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle, "Faction expired");
        std::vector<rts::UnitBatchRequest> units;
        units.reserve(request.units.size());
        std::map<std::string, std::shared_ptr<const rts::PreparedUnitArchetype>> recipes;
        for (std::size_t i = 0; i < request.units.size(); ++i) {
            const auto& unit  = request.units[i];
            auto        found = recipes.find(unit.definition.format());
            if (found == recipes.end()) {
                auto recipe = impl_->resolve(unit.definition);
                if (!recipe) return Result<spawn::PrepareOutcome>::failure(recipe.status());
                found = recipes.emplace(unit.definition.format(), std::move(recipe).takeValue()).first;
            }
            units.push_back({unit.subject, *found->second, static_cast<float>(positions[i].x),
                             static_cast<float>(positions[i].y), impl_->command});
            token->staged.emplace(unit.subject.persistentId(),
                                  Impl::Binding{{}, request.source, request.lifetime, found->second});
        }
        token->receipt.emplace(key(request.source), Impl::Operation{request, token->positions});
        auto native = impl_->domain->prepareUnitBatch(*faction, units);
        if (!native) return Result<spawn::PrepareOutcome>::failure(native.status());
        token->native.emplace(std::move(native).takeValue());
    }
    std::unique_ptr<spawn::IPreparedSpawn> result = std::move(token);
    return Result<spawn::PrepareOutcome>::success(spawn::PrepareOutcome(std::move(result)));
}
Result<spawn::RemovalReceipt> RTSSpawnFactory::remove(const spawn::RemovalRequest& request) {
    if (!impl_->ready())
        return fail<spawn::RemovalReceipt>(DiagnosticCode::PreconditionViolation, "Restore source binding incomplete");
    auto table = impl_->world.lock();
    if (impl_->busy || (table && table->is_deferred()))
        return fail<spawn::RemovalReceipt>(DiagnosticCode::PreconditionViolation,
                                           "Removal requires an idle domain outside Views");
    const bool domainAlive = table && impl_->lifetime.isAlive();
    if (request.operation.isNil() || request.source.owner.isNil() || !request.source.slot.isValid() ||
        request.source.incarnation.isZero() || request.source.epoch == 0 ||
        (request.policy != spawn::DetachPolicy::KeepUnits && request.policy != spawn::DetachPolicy::RemoveUnits))
        return fail<spawn::RemovalReceipt>(DiagnosticCode::InvalidArgument, "Invalid removal request");
    if (!impl_->sourceOwner.isNil() &&
        (impl_->sourceOwner != request.source.owner || impl_->epoch != request.source.epoch ||
         impl_->sourceIncarnation != request.source.incarnation))
        return fail<spawn::RemovalReceipt>(DiagnosticCode::StaleHandle, "Removal world is stale");
    std::set<PersistentId>  seen;
    std::vector<SubjectRef> owned;
    owned.reserve(request.subjects.size());
    for (auto subject : request.subjects) {
        if (!subject.isValid() || !seen.insert(subject.persistentId()).second)
            return fail<spawn::RemovalReceipt>(DiagnosticCode::InvalidArgument, "Duplicate/invalid removal identity");
        auto found = impl_->units.find(subject.persistentId());
        if (found == impl_->units.end() || !found->second.origin) continue;
        if (*found->second.origin != request.source)
            return fail<spawn::RemovalReceipt>(DiagnosticCode::Conflict, "Origin does not own unit");
        auto* live = domainAlive ? ecs::try_get(found->second.unit) : nullptr;
        if (live) owned.push_back(subject);
    }
    impl_->busy = true;
    struct Busy {
        bool& value;
        ~Busy() { value = false; }
    } busy{impl_->busy};
    if (request.policy == spawn::DetachPolicy::RemoveUnits && !owned.empty()) {
        auto native = impl_->domain->prepareUnitRemoval(owned);
        if (!native) return Result<spawn::RemovalReceipt>::failure(native.status());
        auto removed = native.value().commit();
        if (!removed) return Result<spawn::RemovalReceipt>::failure(removed.status());
    }
    for (auto subject : request.subjects) {
        auto found = impl_->units.find(subject.persistentId());
        if (found == impl_->units.end() || !found->second.origin) continue;
        if (request.policy == spawn::DetachPolicy::KeepUnits) {
            found->second.origin.reset();
            found->second.lease = {};
        } else
            impl_->units.erase(found);
    }
    return Result<spawn::RemovalReceipt>::success({request.subjects.size()});
}
Result<UnitSnapshot> RTSSpawnFactory::inspect(SubjectRef subject) const {
    if (!impl_->ready())
        return fail<UnitSnapshot>(DiagnosticCode::PreconditionViolation, "Restore source binding incomplete");
    auto table = impl_->context();
    if (!table) return Result<UnitSnapshot>::failure(table.status());
    auto found = impl_->units.find(subject.persistentId());
    if (found == impl_->units.end()) return fail<UnitSnapshot>(DiagnosticCode::NotFound, "Unit not tracked");
    auto* unit = dynamic_cast<rts::Unit*>(ecs::try_get(found->second.unit));
    if (!unit) return fail<UnitSnapshot>(DiagnosticCode::StaleHandle, "Unit incarnation expired");
    const auto* motion     = existingComponent<rts::Unit::Motion>(*unit);
    const auto* durability = existingComponent<rts::Unit::Durability>(*unit);
    if (!motion || !durability)
        return fail<UnitSnapshot>(DiagnosticCode::InvariantViolation, "Published unit state missing");
    return Result<UnitSnapshot>::success({subject,
                                          found->second.lease.isCurrent() ? found->second.origin : std::nullopt,
                                          {motion->x, motion->y},
                                          durability->state.health});
}
Result<std::vector<spawn::LifecycleInput>> RTSSpawnFactory::collectLifecycle(
    std::span<const rts::RTSFrameDamageEvent> events) {
    using Output = std::vector<spawn::LifecycleInput>;
    if (!impl_->ready())
        return fail<Output>(DiagnosticCode::PreconditionViolation, "Restore source binding incomplete");
    auto table = impl_->world.lock();
    if (impl_->busy || (table && table->is_deferred()))
        return fail<Output>(DiagnosticCode::PreconditionViolation, "Lifecycle requires an idle domain outside Views");
    const bool             domainAlive = table && impl_->lifetime.isAlive();
    std::set<PersistentId> deaths;
    for (const auto& event : events)
        if (event.outcome.previousHealth > 0 && event.outcome.health <= 0 && event.outcome.target.isValid())
            deaths.insert(event.outcome.target.persistentId());
    Output result;
    result.reserve(impl_->units.size());
    std::vector<PersistentId> retired, released;
    retired.reserve(impl_->units.size());
    released.reserve(impl_->units.size());
    auto sequence = impl_->sequence;
    for (const auto& [id, binding] : impl_->units) {
        auto* unit = domainAlive ? dynamic_cast<rts::Unit*>(ecs::try_get(binding.unit)) : nullptr;
        if (!binding.origin) {
            if (!unit) retired.push_back(id);
            continue;
        }
        if (!binding.lease.isCurrent()) {
            released.push_back(id);
            continue;
        }
        const auto* durability = unit ? existingComponent<rts::Unit::Durability>(*unit) : nullptr;
        const bool  died = deaths.contains(id) || (durability && (!durability->alive || durability->state.health <= 0));
        if (!died && unit) continue;
        if (sequence == std::numeric_limits<std::uint64_t>::max())
            return fail<Output>(DiagnosticCode::PreconditionViolation, "Lifecycle sequence exhausted");
        result.push_back({SubjectRef::fromPersistentId(id), ++sequence,
                          died ? spawn::RemovalReason::Death
                               : (domainAlive ? spawn::RemovalReason::Administrative : spawn::RemovalReason::Unloaded),
                          *binding.origin});
        retired.push_back(id);
    }
    for (auto id : retired) impl_->units.erase(id);
    for (auto id : released) {
        auto& binding = impl_->units.at(id);
        binding.origin.reset();
        binding.lease = {};
    }
    impl_->sequence = sequence;
    return Result<Output>::success(std::move(result));
}
}  // namespace eve::rts_spawn
