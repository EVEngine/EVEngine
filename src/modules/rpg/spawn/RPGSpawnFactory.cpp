// eve-hot-path
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/ActorInitialization.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillConditionBindings.h"
#include "rpg/VitalsSystem.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "rpg/spawn/RPGSpawnRestoreStateInternal.inl"
#include "rpg/spawn/RPGUnitCatalogue.h"

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace eve::rpg_spawn {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "rpg.spawn"));
}
struct HandleLess {
    bool operator()(const ecs::EntityHandle& a, const ecs::EntityHandle& b) const {
        if (a.table != b.table) return std::less<ecs::Table*>{}(a.table, b.table);
        if (a.type != b.type) return a.type < b.type;
        return std::tie(a.id, a.generation) < std::tie(b.id, b.generation);
    }
};
using SourceKey = std::tuple<std::uint32_t, std::uint32_t>;
SourceKey sourceKey(const spawn::SpawnerHandle& source) { return {source.slot.index(), source.slot.generation()}; }
SpawnedActorState* stateOf(rpg::RPGActor& actor) {
    auto* buffer = actor.getComponentManager().getComponentBuffer<SpawnedActorState>();
    return buffer && buffer->has(actor.id) ? buffer->ptr(actor.id) : nullptr;
}
}  // namespace
struct RPGSpawnFactory::Impl {
    struct Binding {
        ecs::EntityHandle                   actor;
        std::optional<spawn::SpawnerHandle> origin;
        spawn::SourceLease                  lifetime;
    };
    struct Operation {
        std::uint64_t              ordinal = 0;
        PersistentId               identity;
        spawn::SpawnerHandle       source;
        std::vector<SubjectRef>    subjects;
        std::vector<DefinitionRef> definitions;
        std::vector<spawn::Point>  positions;
        bool matches(const spawn::BatchRequest& request, std::span<const spawn::Point> points) const {
            if (identity != request.operation || source != request.source || subjects.size() != request.units.size() ||
                points.size() != positions.size())
                return false;
            for (std::size_t i = 0; i < request.units.size(); ++i)
                if (subjects[i] != request.units[i].subject || definitions[i] != request.units[i].definition ||
                    positions[i].x != points[i].x || positions[i].y != points[i].y)
                    return false;
            return true;
        }
    };
    std::weak_ptr<ecs::Table>                          world;
    ecs::TableLifetime                                 worldLifetime;
    std::shared_ptr<RPGUnitCatalogue>                  catalogue;
    std::shared_ptr<const RPGSpawnRestoreState>        restored;
    std::shared_ptr<const rpg::SkillConditionBindings> conditions;
    std::map<PersistentId, Binding>                    actors;
    std::map<SourceKey, Operation>                     operations;
    std::map<SourceKey, std::uint64_t>                 restoredFloors;
    bool                                               materialized = false;
    spawn::WorldIncarnation                            sourceIncarnation;
    PersistentId                                       sourceOwner;
    std::uint64_t                                      sourceEpoch = 0;
    std::uint64_t                                      sequence    = 0;
    bool                                               busy        = false;
    struct Prepared                                    final : spawn::IPreparedSpawn {
        std::weak_ptr<Impl>                            owner;
        spawn::BatchRequest                            request;
        std::vector<spawn::Point>                      positions;
        std::vector<std::shared_ptr<const UnitRecipe>> recipes;
        std::vector<rpg::PreparedMemberActor>          prototypes;
        bool                                           committed = false;
        bool                                           isPublished() const noexcept override { return committed; }
        Result<spawn::CommitOutcome>                   commit() override {
            auto state = owner.lock();
            if (!state)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                                                                          "Factory was destroyed before commit");
            if (!request.lifetime.isCurrent() || request.lifetime.source() != request.source)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                                                                          "Spawn source was detached or destroyed before commit");
            auto operation = request.lifetime.operationIdentity(request.ordinal);
            if (!operation || operation.value() != request.operation)
                return failure<spawn::CommitOutcome>(DiagnosticCode::Conflict,
                                                                                                          "Operation does not match its canonical source/ordinal identity");
            if (committed) return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
            auto table = state->world.lock();
            if (!table)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                                                                          "Domain world was destroyed before commit");
            if (state->busy || table->is_deferred())
                return failure<spawn::CommitOutcome>(DiagnosticCode::PreconditionViolation,
                                                                                                          "Commit requires an idle domain outside Views");
            if (!state->sourceOwner.isNil() && state->sourceOwner != request.source.owner)
                return failure<spawn::CommitOutcome>(DiagnosticCode::Conflict,
                                                                                                          "Factory session is bound to another spawn world");
            if (!state->sourceIncarnation.isZero() && state->sourceIncarnation != request.source.incarnation)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle, "Factory world incarnation is stale");
            if (request.source.epoch < state->sourceEpoch)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle, "Spawn world epoch is stale");
            const auto restoredFloor = state->restoredFloors.find(sourceKey(request.source));
            if (request.source.epoch == state->sourceEpoch && restoredFloor != state->restoredFloors.end() &&
                request.ordinal.value() <= restoredFloor->second)
                return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                                                                          "Operation is retired by the restored core");
            const bool newEpoch = request.source.epoch > state->sourceEpoch;
            if (newEpoch && !state->actors.empty())
                return failure<spawn::CommitOutcome>(DiagnosticCode::PreconditionViolation,
                                                                                                          "Detach old sources before changing epoch");
            const auto previous = state->operations.find(sourceKey(request.source));
            if (!newEpoch && previous != state->operations.end()) {
                const auto& op = previous->second;
                if (request.ordinal.value() < op.ordinal)
                    return failure<spawn::CommitOutcome>(DiagnosticCode::StaleHandle,
                                                                                                              "Operation is older than retained source receipt");
                if (request.ordinal.value() == op.ordinal) {
                    if (!op.matches(request, positions))
                        return failure<spawn::CommitOutcome>(DiagnosticCode::Conflict,
                                                                                                                  "Operation reused with different payload");
                    committed = true;
                    return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
                }
            }
            for (const auto& unit : request.units)
                if (state->actors.contains(unit.subject.persistentId()))
                    return failure<spawn::CommitOutcome>(DiagnosticCode::Conflict,
                                                                                                              "Subject identity already belongs to an actor");
            if (!state->restored)
                for (const auto& recipe : recipes) {
                    const auto* skill = rpg::SkillRegistry::find(recipe->member->skillId);
                    if (!skill)
                        return failure<spawn::CommitOutcome>(DiagnosticCode::NotFound, "Prepared unit skill is absent");
                    auto bound = rpg::validateSkillConditionBindings(skill->castCondition, state->conditions.get());
                    if (!bound) return Result<spawn::CommitOutcome>::failure(bound.status());
                }
            std::vector<rpg::PreparedMemberActor> attempts;
            attempts.reserve(prototypes.size());
            for (const auto& prototype : prototypes) {
                if (state->restored) {
                    auto bound = prototype.validateCreationBindings();
                    if (!bound) return Result<spawn::CommitOutcome>::failure(bound.status());
                }
                auto attempt = prototype.clone();
                if (!attempt) return Result<spawn::CommitOutcome>::failure(attempt.status());
                attempts.push_back(std::move(attempt).takeValue());
            }
            std::map<PersistentId, Binding> staged;
            std::map<SourceKey, Operation>  operations;
            Operation                       op;
            op.source    = request.source;
            op.ordinal   = request.ordinal.value();
            op.identity  = request.operation;
            op.positions = positions;
            op.subjects.reserve(request.units.size());
            op.definitions.reserve(request.units.size());
            for (const auto& unit : request.units) {
                staged.emplace(unit.subject.persistentId(), Binding{});
                op.subjects.push_back(unit.subject);
                op.definitions.push_back(unit.definition);
            }
            operations.emplace(sourceKey(request.source), std::move(op));
            std::vector<ecs::EntityHandle> created;
            created.reserve(request.units.size());
            state->busy = true;
            struct Busy {
                bool& value;
                ~Busy() { value = false; }
            } busy{state->busy};
            auto rollback = [&] {
                for (const auto& handle : created)
                    if (auto* actor = ecs::try_get(handle)) ecs::DestroyEntity(actor);
            };
            try {
                for (std::size_t i = 0; i < request.units.size(); ++i) {
                    ecs::ScopedTable current(*table);
                    auto             initialized = attempts[i].create();
                    if (!initialized) {
                        rollback();
                        return Result<spawn::CommitOutcome>::failure(initialized.status());
                    }
                    const auto handle = std::move(initialized).takeValue();
                    created.push_back(handle);
                    auto* actor = static_cast<rpg::RPGActor*>(ecs::try_get(handle));
                    actor->executionOrder()->key =
                        rpg::ActorExecutionKey::fromUuid(request.units[i].subject.persistentId());
                    auto identity = rpg::bindActorIdentity(*actor, request.units[i].subject);
                    if (!identity) {
                        rollback();
                        return Result<spawn::CommitOutcome>::failure(identity.status());
                    }
                    ecs::ComponentRef<SpawnedActorState> link(actor);
                    link.emplace(SpawnedActorState{request.source, positions[i], recipes[i]->definition, recipes[i]});
                    staged.at(request.units[i].subject.persistentId()) = {handle, request.source, request.lifetime};
                }
            } catch (const std::exception& error) {
                rollback();
                return failure<spawn::CommitOutcome>(DiagnosticCode::Failed, error.what());
            } catch (...) {
                rollback();
                return failure<spawn::CommitOutcome>(DiagnosticCode::Failed, "Actor initialization failed");
            }
            state->actors.merge(staged);
            if (newEpoch) {
                state->operations.clear();
                state->restoredFloors.clear();
            } else
                state->operations.erase(sourceKey(request.source));
            state->operations.merge(operations);
            state->sourceIncarnation = request.source.incarnation;
            state->sourceOwner       = request.source.owner;
            state->sourceEpoch       = request.source.epoch;
            prototypes.clear();
            committed = true;
            return Result<spawn::CommitOutcome>::success(spawn::CommitOutcome::Committed);
        }
    };
};
RPGSpawnFactory::RPGSpawnFactory(std::shared_ptr<ecs::Table> world, std::shared_ptr<RPGUnitCatalogue> catalogue,
                                 std::shared_ptr<const rpg::SkillConditionBindings> conditions)
    : impl_(std::make_shared<Impl>()) {
    impl_->world = world;
    if (world) impl_->worldLifetime = world->lifetime();
    impl_->catalogue  = std::move(catalogue);
    impl_->conditions = std::move(conditions);
}
Result<std::unique_ptr<RPGSpawnFactory>> RPGSpawnFactory::prepareRestored(
    std::shared_ptr<ecs::Table> world, std::shared_ptr<const RPGSpawnRestoreState> restored) {
    using R = Result<std::unique_ptr<RPGSpawnFactory>>;
    if (!world || !restored || restored->saveEpoch().isNil())
        return failure<std::unique_ptr<RPGSpawnFactory>>(DiagnosticCode::NotFound,
                                                         "Missing admitted world/content owner");
    if (world->is_deferred())
        return failure<std::unique_ptr<RPGSpawnFactory>>(DiagnosticCode::PreconditionViolation,
                                                         "Factory binding requires a domain outside Views");
    auto factory             = std::make_unique<RPGSpawnFactory>(world, nullptr, restored->conditionOwner());
    factory->impl_->restored = std::move(restored);
    return R::success(std::move(factory));
}
Result<void> RPGSpawnFactory::materializeSavedActors(const spawn::SpawnWorld&    core,
                                                     const RPGSpawnJointRestore& admitted) {
    using R     = Result<void>;
    auto reject = [](DiagnosticCode code, std::string message) {
        return R::failure(Diagnostic::error(code, std::move(message), "rpg.spawn.materialize"));
    };
    if (admitted.saveEpoch().isNil() || !impl_->restored || impl_->materialized || impl_->busy ||
        !impl_->actors.empty() || !impl_->operations.empty() || !impl_->sourceOwner.isNil())
        return reject(DiagnosticCode::PreconditionViolation,
                      "Materialization requires a pristine archive-backed factory");
    const auto owner = admitted.domainOwner();
    if (owner.get() != impl_->restored.get() || owner.owner_before(impl_->restored) ||
        impl_->restored.owner_before(owner))
        return reject(DiagnosticCode::Conflict, "Factory does not own this exact admitted domain");
    auto table = impl_->world.lock();
    if (!table || table->is_deferred())
        return reject(DiagnosticCode::PreconditionViolation,
                      "Materialization requires a live idle domain outside Views");
    {
        auto view = ecs::View<rpg::RPGActor>(*table);
        if (view.begin() != view.end()) return reject(DiagnosticCode::Conflict, "Target RPG domain is not empty");
    }
    const auto                              pendingInput = admitted.pendingLifecycle();
    std::vector<spawn::SavedLifecycleInput> pending(pendingInput.begin(), pendingInput.end());
    auto                                    projection = core.restoreBindings();
    if (!projection) return R::failure(projection.status());
    auto bindings = std::move(projection).takeValue();
    if (!bindings.factory || &bindings.factory->domainFactory() != this || bindings.tick != impl_->restored->tick() ||
        bindings.saveEpoch != impl_->restored->saveEpoch() || bindings.revision != impl_->restored->revision())
        return reject(DiagnosticCode::Conflict, "Core boundary or actual factory binding does not match");
    auto relations = impl_->restored->validateRelations(bindings.membership, bindings.lifecycleSequence, pending);
    if (!relations) return relations;
    auto implementations = impl_->restored->validateImplementationBindings();
    if (!implementations) return implementations;
    impl_->busy = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{impl_->busy};
    const auto&                           archived = *impl_->restored->impl_;
    std::map<PersistentId, Impl::Binding> staged;
    std::map<SourceKey, std::uint64_t>    floors;
    for (const auto& source : bindings.sources) {
        if (!source.lifetime.isCurrent())
            return reject(DiagnosticCode::StaleHandle, "Restored core source was revoked");
        floors.emplace(sourceKey(source.lifetime.source()), source.retiredThrough.value());
    }
    std::vector<spawn::SourceLease> origins;
    origins.reserve(archived.actors.size());
    std::vector<rpg::PreparedMemberActor> attempts;
    attempts.reserve(archived.actors.size());
    std::map<PersistentId, std::size_t> targetRows;
    for (const auto& actor : archived.actors) {
        if (!targetRows.emplace(actor.subject.persistentId(), attempts.size()).second)
            return reject(DiagnosticCode::Conflict, "Duplicate restored actor identity");
        auto valid = actor.state.validateCreationBindings();
        if (!valid) return valid;
        auto clone = actor.state.clone();
        if (!clone) return R::failure(clone.status());
        attempts.push_back(std::move(clone).takeValue());
        spawn::SourceLease lifetime;
        if (actor.origin) {
            auto lease = core.resolveSavedSource(*actor.origin);
            if (!lease) return R::failure(lease.status());
            lifetime = std::move(lease).takeValue();
            staged.emplace(actor.subject.persistentId(), Impl::Binding{});
        }
        origins.push_back(std::move(lifetime));
    }
    std::vector<ecs::EntityHandle> created;
    created.reserve(attempts.size());
    auto rollback = [&] {
        for (const auto& handle : created)
            if (auto* actor = ecs::try_get(handle)) ecs::DestroyEntity(actor);
    };
    try {
        for (std::size_t i = 0; i < attempts.size(); ++i) {
            ecs::ScopedTable current(*table);
            // Allocate every root before binding casts: later rows may be earlier rows' targets.
            auto* fresh  = rpg::RPGActor::createActor();
            auto  handle = ecs::handle_of(fresh);
            created.push_back(handle);
            const auto&                         saved  = archived.actors[i];
            const auto&                         recipe = archived.units[saved.unit];
            std::optional<spawn::SpawnerHandle> source;
            if (saved.origin) source = origins[i].source();
            auto* actor    = static_cast<rpg::RPGActor*>(ecs::try_get(handle));
            auto  identity = rpg::bindActorIdentity(*actor, saved.subject);
            if (!identity) {
                rollback();
                return R::failure(identity.status());
            }
            ecs::ComponentRef<SpawnedActorState> link(actor);
            link.emplace(SpawnedActorState{source, saved.position, recipe->definition, recipe});
            if (source) staged.at(saved.subject.persistentId()) = {handle, source, origins[i]};
        }
        for (std::size_t i = 0; i < attempts.size(); ++i) {
            auto target = attempts[i].restoredCastTarget();
            if (!target) {
                rollback();
                return R::failure(target.status());
            }
            std::optional<rpg::CastTargetBinding> binding;
            if (target.value()) {
                const auto row = targetRows.find(target.value()->persistentId());
                if (row == targetRows.end()) {
                    rollback();
                    return reject(DiagnosticCode::NotFound, "Cast target missing");
                }
                binding = rpg::CastTargetBinding{*target.value(), created[row->second]};
            }
            auto* actor     = static_cast<rpg::RPGActor*>(ecs::try_get(created[i]));
            auto  published = attempts[i].publishTo(*actor, binding);
            if (!published) {
                rollback();
                return R::failure(published.status());
            }
        }
    } catch (const std::exception& error) {
        rollback();
        return reject(DiagnosticCode::Failed, error.what());
    } catch (...) {
        rollback();
        return reject(DiagnosticCode::Failed, "Saved actor materialization failed");
    }
    impl_->actors.swap(staged);
    impl_->restoredFloors.swap(floors);
    impl_->sequence = pending.empty() ? bindings.lifecycleSequence : pending.back().sequence;
    if (!bindings.sources.empty()) {
        const auto source        = bindings.sources.front().lifetime.source();
        impl_->sourceIncarnation = source.incarnation;
        impl_->sourceOwner       = source.owner;
        impl_->sourceEpoch       = source.epoch;
    }
    impl_->materialized = true;
    return R::success();
}
RPGSpawnFactory::~RPGSpawnFactory() = default;
Result<RPGSpawnCapture> RPGSpawnFactory::capture(PersistentId saveEpoch, std::span<const DefinitionRef> references,
                                                 std::span<const std::string> eventSkills,
                                                 std::span<const std::string> eventEffects) const {
    if (saveEpoch.isNil())
        return failure<RPGSpawnCapture>(DiagnosticCode::InvalidArgument, "Joint save epoch is required");
    auto world = impl_->world.lock();
    if (!world || (!impl_->catalogue && !impl_->restored))
        return failure<RPGSpawnCapture>(DiagnosticCode::NotFound, "Missing domain/content owner");
    if (impl_->busy || world->is_deferred())
        return failure<RPGSpawnCapture>(DiagnosticCode::PreconditionViolation,
                                        "Capture requires an idle domain outside Views");
    impl_->busy = true;
    struct Reset {
        bool& busy;
        ~Reset() { busy = false; }
    } reset{impl_->busy};
    std::vector<UnitRecipe> future;
    future.reserve(references.size());
    for (const auto& reference : references) {
        if (impl_->restored) {
            auto recipe = impl_->restored->futureUnit(reference);
            if (!recipe) return Result<RPGSpawnCapture>::failure(recipe.status());
            auto candidate = impl_->restored->prepareFutureUnit(reference);
            if (!candidate) return Result<RPGSpawnCapture>::failure(candidate.status());
            auto bound = candidate.value().validateCreationBindings();
            if (!bound) return Result<RPGSpawnCapture>::failure(bound.status());
            future.push_back(*recipe.value());
        } else {
            auto recipe = impl_->catalogue->resolve(reference);
            if (!recipe) return Result<RPGSpawnCapture>::failure(recipe.status());
            future.push_back(std::move(recipe).takeValue());
        }
    }
    return RPGSpawnCapture::captureWithFuture(*world, saveEpoch, future, impl_->conditions, eventSkills, eventEffects);
}
Result<spawn::PrepareOutcome> RPGSpawnFactory::prepare(const spawn::BatchRequest&    request,
                                                       std::span<const spawn::Point> positions) {
    auto world = impl_->world.lock();
    if (!world || (!impl_->catalogue && !impl_->restored))
        return failure<spawn::PrepareOutcome>(DiagnosticCode::NotFound, "Missing domain/content owner");
    if (impl_->busy || world->is_deferred())
        return failure<spawn::PrepareOutcome>(DiagnosticCode::PreconditionViolation,
                                              "Preparation requires an idle domain outside Views");
    if (request.operation.isNil() || request.source.owner.isNil() || !request.source.slot.isValid() ||
        request.source.incarnation.isZero() || request.source.epoch == 0 || request.ordinal.isZero() ||
        request.units.empty() || request.units.size() != positions.size())
        return failure<spawn::PrepareOutcome>(DiagnosticCode::InvalidArgument, "Invalid spawn request");
    if (!request.lifetime.isCurrent() || request.lifetime.source() != request.source)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle,
                                              "Spawn source lifetime is stale or mismatched");
    auto operation = request.lifetime.operationIdentity(request.ordinal);
    if (!operation || operation.value() != request.operation)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::Conflict,
                                              "Operation does not match its canonical source/ordinal identity");
    auto token     = std::make_unique<Impl::Prepared>();
    token->owner   = impl_;
    token->request = request;
    token->positions.assign(positions.begin(), positions.end());
    token->recipes.reserve(request.units.size());
    token->prototypes.reserve(request.units.size());
    if (!impl_->sourceOwner.isNil() && impl_->sourceOwner != request.source.owner)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::Conflict,
                                              "Factory session belongs to another spawn world");
    if (!impl_->sourceIncarnation.isZero() && impl_->sourceIncarnation != request.source.incarnation)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle, "Factory world incarnation is stale");
    if (request.source.epoch < impl_->sourceEpoch)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle, "Spawn world epoch is stale");
    const auto restoredFloor = impl_->restoredFloors.find(sourceKey(request.source));
    if (request.source.epoch == impl_->sourceEpoch && restoredFloor != impl_->restoredFloors.end() &&
        request.ordinal.value() <= restoredFloor->second)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle, "Operation is retired by the restored core");
    const auto previous = impl_->operations.find(sourceKey(request.source));
    const bool retained = request.source.epoch == impl_->sourceEpoch && previous != impl_->operations.end();
    if (retained && request.ordinal.value() < previous->second.ordinal)
        return failure<spawn::PrepareOutcome>(DiagnosticCode::StaleHandle,
                                              "Operation precedes retained source receipt");
    const bool replay = retained && request.ordinal.value() == previous->second.ordinal;
    if (replay && !previous->second.matches(request, positions))
        return failure<spawn::PrepareOutcome>(DiagnosticCode::Conflict, "Operation reused with different payload");
    token->committed = replay;
    std::map<std::string, std::shared_ptr<const UnitRecipe>> pinned;
    std::set<PersistentId>                                   identities;
    for (std::size_t i = 0; i < request.units.size(); ++i) {
        const auto& unit = request.units[i];
        if (!unit.subject.isValid() || !identities.insert(unit.subject.persistentId()).second ||
            !std::isfinite(positions[i].x) || !std::isfinite(positions[i].y))
            return failure<spawn::PrepareOutcome>(DiagnosticCode::InvalidArgument, "Invalid identity or position");
        if (replay) continue;
        auto found = pinned.find(unit.definition.format());
        if (found == pinned.end()) {
            if (impl_->restored) {
                auto recipe = impl_->restored->futureUnit(unit.definition);
                if (!recipe) return Result<spawn::PrepareOutcome>::failure(recipe.status());
                found = pinned.emplace(unit.definition.format(), std::move(recipe).takeValue()).first;
            } else {
                auto recipe = impl_->catalogue->resolve(unit.definition);
                if (!recipe) return Result<spawn::PrepareOutcome>::failure(recipe.status());
                found = pinned
                            .emplace(unit.definition.format(),
                                     std::make_shared<const UnitRecipe>(std::move(recipe).takeValue()))
                            .first;
            }
        }
        auto prototype = impl_->restored ? impl_->restored->prepareFutureUnit(unit.definition)
                                         : rpg::PreparedMemberActor::prepare(*found->second->member, impl_->conditions);
        if (!prototype) return Result<spawn::PrepareOutcome>::failure(prototype.status());
        if (!impl_->restored && !impl_->conditions) {
            const auto* skill = rpg::SkillRegistry::find(found->second->member->skillId);
            if (!skill) return failure<spawn::PrepareOutcome>(DiagnosticCode::NotFound, "Unit skill content is absent");
            auto bound = rpg::validateSkillConditionBindings(skill->castCondition, nullptr);
            if (!bound) return Result<spawn::PrepareOutcome>::failure(bound.status());
        }
        token->prototypes.push_back(std::move(prototype).takeValue());
        token->recipes.push_back(found->second);
    }
    std::unique_ptr<spawn::IPreparedSpawn> prepared = std::move(token);
    return Result<spawn::PrepareOutcome>::success(spawn::PrepareOutcome(std::move(prepared)));
}
Result<spawn::RemovalReceipt> RPGSpawnFactory::remove(const spawn::RemovalRequest& request) {
    if (impl_->busy) return failure<spawn::RemovalReceipt>(DiagnosticCode::PreconditionViolation, "Factory is busy");
    auto world = impl_->world.lock();
    if (world && world->is_deferred())
        return failure<spawn::RemovalReceipt>(DiagnosticCode::PreconditionViolation,
                                              "Removal requires a domain outside Views");
    if (request.operation.isNil() || request.source.owner.isNil() || !request.source.slot.isValid() ||
        request.source.incarnation.isZero() || request.source.epoch == 0 ||
        (request.policy != spawn::DetachPolicy::KeepUnits && request.policy != spawn::DetachPolicy::RemoveUnits))
        return failure<spawn::RemovalReceipt>(DiagnosticCode::InvalidArgument, "Invalid removal request");
    if (!impl_->sourceOwner.isNil() &&
        (impl_->sourceOwner != request.source.owner || impl_->sourceEpoch != request.source.epoch ||
         impl_->sourceIncarnation != request.source.incarnation))
        return failure<spawn::RemovalReceipt>(DiagnosticCode::StaleHandle, "Removal source world is stale");
    std::set<PersistentId> seen;
    for (const auto& subject : request.subjects) {
        if (!subject.isValid() || !seen.insert(subject.persistentId()).second)
            return failure<spawn::RemovalReceipt>(DiagnosticCode::InvalidArgument, "Invalid removal identity");
        const auto found = impl_->actors.find(subject.persistentId());
        if (found != impl_->actors.end() && found->second.origin && *found->second.origin != request.source)
            return failure<spawn::RemovalReceipt>(DiagnosticCode::Conflict, "Removal source does not own actor");
    }
    impl_->busy = true;
    struct Busy {
        bool& value;
        ~Busy() { value = false; }
    } busy{impl_->busy};
    for (const auto& subject : request.subjects) {
        const auto found = impl_->actors.find(subject.persistentId());
        if (found == impl_->actors.end()) continue;
        if (world)
            if (auto* actor = dynamic_cast<rpg::RPGActor*>(ecs::try_get(found->second.actor))) {
                if (request.policy == spawn::DetachPolicy::RemoveUnits)
                    ecs::DestroyEntity(actor);
                else if (auto* link = stateOf(*actor))
                    link->origin.reset();
            }
        impl_->actors.erase(found);
    }
    return Result<spawn::RemovalReceipt>::success({request.subjects.size()});
}
Result<ActorSnapshot> RPGSpawnFactory::inspect(SubjectRef subject) const {
    auto world = impl_->world.lock();
    if (!world) return failure<ActorSnapshot>(DiagnosticCode::StaleHandle, "Domain world is gone");
    if (impl_->busy)
        return failure<ActorSnapshot>(DiagnosticCode::PreconditionViolation, "Cannot inspect provisional actors");
    const auto found = impl_->actors.find(subject.persistentId());
    if (found == impl_->actors.end())
        return failure<ActorSnapshot>(DiagnosticCode::NotFound, "Actor identity not tracked");
    auto* actor = dynamic_cast<rpg::RPGActor*>(ecs::try_get(found->second.actor));
    if (!actor) return failure<ActorSnapshot>(DiagnosticCode::StaleHandle, "Actor incarnation was destroyed");
    auto* state = stateOf(*actor);
    if (!state) return failure<ActorSnapshot>(DiagnosticCode::InvariantViolation, "Spawn actor state missing");
    const auto registered = rpg::actorSubject(*actor);
    if (!registered || *registered != subject)
        return failure<ActorSnapshot>(DiagnosticCode::InvariantViolation, "Actor identity differs from routing index");
    return Result<ActorSnapshot>::success(
        {*registered, found->second.lifetime.isCurrent() ? found->second.origin : std::nullopt, state->position,
         actor->getCurrent("hp"), actor->getBaseAttribute("attack"), state->definition});
}
Result<double> RPGSpawnFactory::applyDamage(SubjectRef subject, double amount) {
    if (!subject.isValid() || !std::isfinite(amount) || amount < 0)
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "Valid subject and finite nonnegative HP damage required");
    auto world = impl_->world.lock();
    if (!world) return failure<double>(DiagnosticCode::StaleHandle, "Domain world is gone");
    if (world->is_deferred())
        return failure<double>(DiagnosticCode::PreconditionViolation, "Damage requires a domain outside Views");
    auto observed = inspect(subject);
    if (!observed) return Result<double>::failure(observed.status());
    if (!observed.value().origin)
        return failure<double>(DiagnosticCode::StaleHandle, "Actor source lifetime is retired");
    impl_->busy = true;
    struct Reset {
        bool& busy;
        ~Reset() { busy = false; }
    } reset{impl_->busy};
    ecs::ScopedTable scope(*world);
    const auto       found = impl_->actors.find(subject.persistentId());
    auto*            actor = dynamic_cast<rpg::RPGActor*>(ecs::try_get(found->second.actor));
    return Result<double>::success(rpg::VitalsSystem::takeDamage(actor, "hp", amount));
}
Result<std::vector<spawn::LifecycleInput>> RPGSpawnFactory::collectLifecycle(std::span<const rpg::VitalsEvent> events) {
    using R = Result<std::vector<spawn::LifecycleInput>>;
    if (impl_->busy)
        return failure<std::vector<spawn::LifecycleInput>>(DiagnosticCode::PreconditionViolation, "Factory is busy");
    auto world = impl_->world.lock();
    if (world && world->is_deferred())
        return failure<std::vector<spawn::LifecycleInput>>(DiagnosticCode::PreconditionViolation,
                                                           "Lifecycle bridge requires a domain outside Views");
    std::vector<spawn::LifecycleInput> result;
    result.reserve(impl_->actors.size());
    std::vector<PersistentId> retired;
    retired.reserve(impl_->actors.size());
    std::set<ecs::EntityHandle, HandleLess> deaths;
    for (const auto& event : events)
        if (event.action == "death" && event.resource == "hp" && event.actor.handle().table &&
            event.actor.isFromWorld(impl_->worldLifetime))
            deaths.insert(event.actor.handle());
    auto sequence = impl_->sequence;
    for (const auto& [id, binding] : impl_->actors) {
        if (!binding.origin) continue;
        if (!binding.lifetime.isCurrent()) {
            retired.push_back(id);
            continue;
        }
        const bool died  = deaths.contains(binding.actor);
        const bool stale = !world || ecs::try_get(binding.actor) == nullptr;
        if (!died && !stale) continue;
        if (sequence == std::numeric_limits<std::uint64_t>::max())
            return failure<std::vector<spawn::LifecycleInput>>(DiagnosticCode::PreconditionViolation,
                                                               "Lifecycle sequence exhausted");
        result.push_back({SubjectRef::fromPersistentId(id), ++sequence,
                          died ? spawn::RemovalReason::Death
                               : (world ? spawn::RemovalReason::Administrative : spawn::RemovalReason::Unloaded),
                          *binding.origin});
        retired.push_back(id);
    }
    for (const auto& id : retired) {
        auto& binding = impl_->actors.at(id);
        if (world)
            if (auto* actor = dynamic_cast<rpg::RPGActor*>(ecs::try_get(binding.actor)))
                if (auto* link = stateOf(*actor)) link->origin.reset();
        impl_->actors.erase(id);
    }
    impl_->sequence = sequence;
    return R::success(std::move(result));
}
}  // namespace eve::rpg_spawn
