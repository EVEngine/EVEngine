// eve-hot-path
#include "spawn/SpawnWorld.h"
#include <set>
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnIdentityInternal.h"
#include "spawn/SpawnRandom.h"
#include "spawn/SpawnRegion.h"
#include "spawn/SpawnRegionInternal.inl"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace eve::spawn {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "spawn.world"));
}
Diagnostic failureDiagnostic(const Status& status) {
    const auto* diagnostic = status.primaryDiagnostic();
    return diagnostic != nullptr
               ? *diagnostic
               : Diagnostic::error(DiagnosticCode::Failed, "Provider returned failure without diagnostic");
}
template <class F>
auto invokeProvider(F&& callback) -> decltype(callback()) {
    using R = decltype(callback());
    try {
        return callback();
    } catch (const std::exception& error) {
        return R::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), "spawn.provider"));
    } catch (...) {
        return R::failure(Diagnostic::error(DiagnosticCode::CallbackFailure, "Provider threw an unknown exception",
                                            "spawn.provider"));
    }
}
std::uint64_t uniform(std::uint64_t& state, std::uint64_t bound) {
    const auto threshold = (std::uint64_t(0) - bound) % bound;
    for (;;) {
        const auto value = nextRandomWord(state);
        if (value >= threshold) return value % bound;
    }
}
std::uint64_t streamSeed(std::uint64_t seed, std::string_view id, std::string_view role) {
    auto state = seed ^ 14695981039346656037ULL;
    for (const unsigned char c : id) state = (state ^ c) * 1099511628211ULL;
    state = (state ^ 0xffU) * 1099511628211ULL;
    for (const unsigned char c : role) state = (state ^ c) * 1099511628211ULL;
    return state;
}
std::uint32_t groupSize(const std::vector<Member>& members) {
    std::uint32_t result = 0;
    for (const auto& member : members) result += member.count;
    return result;
}
std::uint32_t maximumBatch(const TemplateDefinition& definition) {
    if (std::holds_alternative<OnceSchedule>(definition.schedule))
        return groupSize(std::get<FixedContent>(definition.content).members);
    if (const auto* interval = std::get_if<IntervalSchedule>(&definition.schedule)) return interval->count;
    if (const auto* population = std::get_if<PopulationSchedule>(&definition.schedule)) return population->target;
    std::uint32_t result = 0;
    for (const auto& wave : std::get<WaveSchedule>(definition.schedule).waves)
        result = std::max(result, groupSize(wave.members));
    return result;
}
}  // namespace

SavedSpawnerKey savedSpawnerKey(const SpawnerHandle& source) noexcept {
    return {source.owner, source.epoch, source.slot.index(), source.slot.generation()};
}
SavedLifecycleInput savedLifecycleInput(const LifecycleInput& input) noexcept {
    return {input.subject, input.sequence, input.reason, savedSpawnerKey(input.source)};
}

// Owning data copy is used only for admitted, authority-free restore attempts.
struct SpawnWorldFacts {
    struct Tracked {
        SubjectRef  subject;
        std::size_t wave = 0;
    };
    struct Slot {
        std::shared_ptr<const std::byte> marker;
        SourceLease                      lifetime;
        bool                             occupied   = true;
        std::uint32_t                    generation = 1;
        bool                             announced  = false;
        InstanceDefinition               definition;
        Phase                            phase         = Phase::Dormant;
        BlockReason                      blocked       = BlockReason::None;
        bool                             active        = false;
        std::int64_t                     elapsed       = 0;
        std::int64_t                     anchor        = 0;
        std::int64_t                     due           = 0;
        std::uint32_t                    alive         = 0;
        std::uint64_t                    total         = 0;
        std::size_t                      wave          = 0;
        bool                             initialDone   = false;
        bool                             wavePublished = false;
        std::uint64_t                    operation     = 0;
        std::uint64_t                    contentRandom = 0, placementRandom = 0, cooldownRandom = 0;
        std::vector<std::int64_t>        replacements;
        std::vector<Tracked>             tracked;
        std::optional<BatchRequest>      pending;
    };
    WorldIncarnation                                            incarnation;
    PersistentId                                                restoredSaveEpoch;
    Revision                                                    restoredRevision;
    PersistentId                                                owner;
    std::uint64_t                                               seed  = 0;
    std::uint64_t                                               epoch = 1;
    WorkLimits                                                  limits;
    std::weak_ptr<SpawnProviders>                               providers;
    std::vector<Slot>                                           slots;
    std::vector<BudgetDefinition>                               budgets;
    std::vector<std::uint32_t>                                  budgetAlive;
    std::map<PersistentId, std::pair<std::size_t, std::size_t>> subjects;
    std::int64_t                                                now            = 0;
    std::uint64_t                                               tick           = 0;
    std::uint64_t                                               sequence       = 0;
    std::size_t                                                 cursor         = 0;
    bool                                                        advancing      = false;
    bool                                                        attached       = false;
    bool                                                        notifying      = false;
    std::uint64_t                                               outputSequence = 0;
    std::vector<ControlCommand>                                 commands;
    std::vector<SpawnEvent>                                     pendingEvents;
};
struct SpawnWorld::Impl : SpawnWorldFacts {
    Observer<SpawnEvent> observers;
    Impl() = default;
    explicit Impl(const SpawnWorldFacts& facts) : SpawnWorldFacts(facts) {}

    std::int64_t age(const Slot& slot) const { return slot.elapsed + (slot.active ? now - slot.anchor : 0); }
    bool         isValid(SpawnerHandle handle) const {
        return handle.incarnation == incarnation && !incarnation.isZero() && handle.owner == owner &&
               handle.epoch == epoch && handle.slot.index() < slots.size() && slots[handle.slot.index()].occupied &&
               handle.slot.generation() == slots[handle.slot.index()].generation;
    }
    SpawnerHandle handle(std::size_t index) const {
        return {RuntimeHandle<SpawnerTag>(static_cast<std::uint32_t>(index), slots[index].generation), owner, epoch,
                incarnation};
    }
    std::int64_t deadline(Slot& slot, std::int64_t time, Duration delay) const {
        const auto duration = delay.nanoseconds();
        if (duration > std::numeric_limits<std::int64_t>::max() - time) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::ArithmeticOverflow;
            return std::numeric_limits<std::int64_t>::max();
        }
        return time + duration;
    }
    void emit(std::vector<SpawnEvent>& events, std::size_t index, SpawnEventKind kind, std::size_t wave = 0) const {
        SpawnEvent event;
        event.tick   = SimulationTick(tick);
        event.source = handle(index);
        event.kind   = kind;
        event.reason = slots[index].blocked;
        if (slots[index].pending) event.operation = slots[index].pending->operation;
        event.wave = wave;
        events.push_back(std::move(event));
    }
    struct Transition {
        Impl&                    world;
        std::vector<SpawnEvent>& events;
        std::size_t              index;
        Phase                    phase;
        BlockReason              reason;
        std::size_t              wave;
        Transition(Impl& owner, std::vector<SpawnEvent>& target, std::size_t i)
            : world(owner),
              events(target),
              index(i),
              phase(owner.slots[i].phase),
              reason(owner.slots[i].blocked),
              wave(owner.slots[i].wave) {}
        ~Transition() {
            const auto& slot = world.slots[index];
            if (slot.wave != wave) world.emit(events, index, SpawnEventKind::WaveCompleted, wave);
            if (slot.blocked != reason)
                world.emit(events, index,
                           slot.blocked == BlockReason::None ? SpawnEventKind::Unblocked : SpawnEventKind::Blocked,
                           slot.wave);
            if (slot.phase != phase) {
                if (slot.phase == Phase::Faulted)
                    world.emit(events, index, SpawnEventKind::Faulted, slot.wave);
                else if (slot.phase == Phase::Completed)
                    world.emit(events, index, SpawnEventKind::Completed, slot.wave);
                else if (phase == Phase::Dormant && slot.phase == Phase::Running)
                    world.emit(events, index, SpawnEventKind::Activated, slot.wave);
                else if (phase == Phase::Running && slot.phase == Phase::Dormant)
                    world.emit(events, index, SpawnEventKind::Paused, slot.wave);
            }
        }
    };
    void dispatch(std::vector<SpawnEvent>& events, std::vector<Diagnostic>& diagnostics) {
        notifying = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{notifying};
        for (auto& event : events) {
            event.sequence      = EventSequence(++outputSequence);
            const auto failures = observers.notifyChecked(
                [&]() {
                    diagnostics.push_back(Diagnostic::error(DiagnosticCode::CallbackFailure,
                                                            "Spawn event observer threw", "spawn.events"));
                },
                event);
            (void)failures;
        }
    }
    Result<Change> apply(ControlCommand command, std::vector<SpawnEvent>& events) {
        if (!isValid(command.source)) return failure<Change>(DiagnosticCode::StaleHandle, "Queued source is stale");
        const auto index = command.source.slot.index();
        auto&      slot  = slots[index];
        Transition transition{*this, events, index};
        if (command.kind == ControlKind::Activate || command.kind == ControlKind::Pause) {
            const bool active = command.kind == ControlKind::Activate;
            if (slot.active == active) return Result<Change>::success(Change::Unchanged);
            slot.elapsed   = age(slot);
            slot.anchor    = now;
            slot.active    = active;
            slot.announced = true;
            if (slot.phase != Phase::Faulted && slot.phase != Phase::Completed)
                slot.phase = active ? Phase::Running : Phase::Dormant;
            return Result<Change>::success(Change::Changed);
        }
        if (command.kind == ControlKind::RetryFault) {
            if (slot.phase != Phase::Faulted) return Result<Change>::success(Change::Unchanged);
            if (slot.blocked != BlockReason::FactoryFailed && slot.blocked != BlockReason::PlacementBlocked &&
                slot.blocked != BlockReason::ProviderUnavailable)
                return failure<Change>(DiagnosticCode::PreconditionViolation,
                                       "This fault requires explicit source restart, not retry");
            slot.phase   = slot.active ? Phase::Running : Phase::Dormant;
            slot.blocked = BlockReason::None;
            slot.due     = age(slot);
            emit(events, index, SpawnEventKind::RetryRequested, slot.wave);
            return Result<Change>::success(Change::Changed);
        }
        if (command.kind != ControlKind::ReconcilePopulation)
            return failure<Change>(DiagnosticCode::InvalidArgument, "Unknown spawn control");
        const auto* population = std::get_if<PopulationSchedule>(&slot.definition.revision->definition().schedule);
        if (!population || slot.phase == Phase::Faulted || slot.phase == Phase::Completed || slot.pending)
            return failure<Change>(DiagnosticCode::PreconditionViolation,
                                   "Reconciliation requires an idle population schedule without a pending batch");
        const auto tracked = static_cast<std::uint64_t>(slot.alive) + slot.replacements.size();
        if (tracked >= population->target) return Result<Change>::success(Change::Unchanged);
        const auto  count = population->target - tracked;
        const auto& total = slot.definition.revision->definition().limits.total;
        if (total && (slot.total > *total || tracked - slot.alive + count > *total - slot.total))
            return failure<Change>(DiagnosticCode::PreconditionViolation, "Reconciliation exceeds lifetime quota");
        slot.replacements.reserve(slot.replacements.size() + count);
        slot.replacements.insert(slot.replacements.end(), count, age(slot));
        std::sort(slot.replacements.begin(), slot.replacements.end());
        slot.initialDone = true;
        slot.due         = age(slot);
        emit(events, index, SpawnEventKind::Reconciled, slot.wave);
        return Result<Change>::success(Change::Changed);
    }
    void scheduleReplacement(Slot& slot, const PopulationSchedule& population) {
        const auto minimum = population.minimumDelay.nanoseconds();
        const auto width   = static_cast<std::uint64_t>(population.maximumDelay.nanoseconds() - minimum) + 1;
        slot.replacements.push_back(deadline(
            slot, age(slot), Duration(minimum + static_cast<std::int64_t>(uniform(slot.cooldownRandom, width)))));
    }
    void finishInitialPopulation(Slot& slot, const PopulationSchedule& population) {
        slot.replacements.reserve(slot.replacements.size() + population.target - slot.alive);
        while (static_cast<std::uint64_t>(slot.alive) + slot.replacements.size() < population.target)
            scheduleReplacement(slot, population);
        std::sort(slot.replacements.begin(), slot.replacements.end());
        slot.initialDone = true;
    }
    BatchRequest batch(Slot& slot, std::uint32_t count, const std::vector<Member>* fixed) {
        BatchRequest request;
        request.ordinal  = OperationOrdinal(++slot.operation);
        request.lifetime = slot.lifetime;
        request.operation =
            slot.lifetime.operationIdentity(request.ordinal).expect("Current source has a valid operation identity");
        request.units.reserve(count);
        if (fixed != nullptr) {
            for (const auto& member : *fixed)
                for (std::uint32_t i = 0; i < member.count; ++i)
                    request.units.push_back(
                        {slot.lifetime.unitIdentity(request.ordinal, static_cast<std::uint32_t>(request.units.size()))
                             .expect("Current member identity"),
                         member.unit});
        } else {
            const auto&   entries = std::get<WeightedContent>(slot.definition.revision->definition().content).entries;
            std::uint64_t sum     = 0;
            for (const auto& entry : entries) sum += entry.weight;
            for (std::uint32_t i = 0; i < count; ++i) {
                auto draw = uniform(slot.contentRandom, sum);
                for (const auto& entry : entries) {
                    if (draw < entry.weight) {
                        request.units.push_back(
                            {slot.lifetime.unitIdentity(request.ordinal, i).expect("Current member identity"),
                             entry.unit});
                        break;
                    }
                    draw -= entry.weight;
                }
            }
        }
        return request;
    }
};

SpawnWorld::SpawnWorld(PersistentId owner, std::uint64_t seed, WorkLimits limits) : impl_(std::make_unique<Impl>()) {
    impl_->incarnation = WorldIncarnation(detail::nextSpawnIncarnation());
    impl_->owner       = owner;
    impl_->seed        = seed;
    impl_->limits      = limits;
}
SpawnWorld::~SpawnWorld()                                = default;
SpawnWorld::SpawnWorld(SpawnWorld&&) noexcept            = default;
SpawnWorld& SpawnWorld::operator=(SpawnWorld&&) noexcept = default;

Result<std::vector<SpawnerHandle>> SpawnWorld::attach(const CompiledMap&                     map,
                                                      const std::shared_ptr<SpawnProviders>& providers) {
    if (!impl_ || impl_->advancing || impl_->notifying ||
        std::any_of(impl_->slots.begin(), impl_->slots.end(), [](const auto& slot) { return slot.occupied; }))
        return failure<std::vector<SpawnerHandle>>(DiagnosticCode::PreconditionViolation,
                                                   "World must be empty and idle");
    auto& world = *impl_;
    if (world.incarnation.isZero() || world.owner.isNil() || world.limits.instancesPerTick == 0 ||
        world.limits.batchesPerTick == 0 || world.limits.unitsPerTick == 0 || world.limits.candidatesPerTick == 0 ||
        world.limits.queuedCommands == 0 || map.instances().size() >= RuntimeHandle<SpawnerTag>::invalidIndex)
        return failure<std::vector<SpawnerHandle>>(DiagnosticCode::InvalidArgument, "Invalid owner or work limits");
    if (!providers || !providers->placement || !providers->factory)
        return failure<std::vector<SpawnerHandle>>(DiagnosticCode::NotFound, "Required spawn providers are absent");
    world.advancing = true;
    struct AttachGuard {
        bool& flag;
        ~AttachGuard() { flag = false; }
    } attachGuard{world.advancing};
    if (world.attached && world.epoch == std::numeric_limits<std::uint64_t>::max())
        return failure<std::vector<SpawnerHandle>>(DiagnosticCode::PreconditionViolation, "World epoch exhausted");
    const auto              epoch = world.epoch + (world.attached ? 1 : 0);
    std::vector<Impl::Slot> slots;
    for (const auto& instance : map.instances()) {
        const auto& definition = instance.revision->definition();
        const auto  maximum    = maximumBatch(definition);
        if (maximum > world.limits.unitsPerTick ||
            static_cast<std::uint64_t>(maximum) * definition.placement.attempts > world.limits.candidatesPerTick)
            return failure<std::vector<SpawnerHandle>>(DiagnosticCode::InvalidArgument,
                                                       "Atomic batch exceeds tick work limits");
        auto validated =
            invokeProvider([&] { return providers->placement->validate(definition.placement, instance.region); });
        if (!validated) return Result<std::vector<SpawnerHandle>>::failure(validated.status());
        Impl::Slot slot;
        slot.definition      = instance;
        slot.anchor          = world.now;
        slot.active          = std::holds_alternative<MapLoadedActivation>(instance.activation);
        slot.phase           = slot.active ? Phase::Running : Phase::Dormant;
        slot.contentRandom   = streamSeed(world.seed, instance.id, "content");
        slot.placementRandom = streamSeed(world.seed, instance.id, "placement");
        slot.cooldownRandom  = streamSeed(world.seed, instance.id, "cooldown");
        if (const auto* interval = std::get_if<IntervalSchedule>(&definition.schedule))
            slot.due = interval->initialDelay.nanoseconds();
        if (const auto* waves = std::get_if<WaveSchedule>(&definition.schedule))
            slot.due = waves->waves[0].delay.nanoseconds();
        slots.push_back(std::move(slot));
    }
    std::sort(slots.begin(), slots.end(),
              [](const auto& a, const auto& b) { return a.definition.id < b.definition.id; });
    std::vector<SpawnerHandle> handles;
    handles.reserve(slots.size());
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const SpawnerHandle handle{RuntimeHandle<SpawnerTag>(static_cast<std::uint32_t>(index), 1), world.owner, epoch,
                                   world.incarnation};
        slots[index].marker   = std::make_shared<const std::byte>();
        slots[index].lifetime = SourceLease(handle, slots[index].definition.id, slots[index].marker);
        handles.push_back(handle);
    }
    auto                       budgets = map.budgets();
    std::vector<std::uint32_t> counts(budgets.size(), 0);
    world.sequence = 0;
    world.commands.clear();
    world.epoch             = epoch;
    world.cursor            = 0;
    world.slots             = std::move(slots);
    world.budgets           = std::move(budgets);
    world.budgetAlive       = std::move(counts);
    world.providers         = providers;
    world.restoredSaveEpoch = {};
    world.restoredRevision  = {};
    world.attached          = true;
    return Result<std::vector<SpawnerHandle>>::success(std::move(handles));
}
Result<void> SpawnWorld::retireSources() {
    if (!impl_ || impl_->advancing || impl_->notifying)
        return failure<void>(DiagnosticCode::PreconditionViolation, "Retirement requires an idle core owner");
    impl_->attached    = false;
    impl_->incarnation = {};
    for (auto& slot : impl_->slots) slot.marker.reset();
    return Result<void>::success();
}
Result<SourceLease> SpawnWorld::sourceLease(SpawnerHandle handle) const {
    if (!impl_ || !impl_->isValid(handle))
        return failure<SourceLease>(DiagnosticCode::StaleHandle, "Source incarnation is stale");
    return Result<SourceLease>::success(impl_->slots[handle.slot.index()].lifetime);
}
Result<void> SpawnWorld::setActive(SpawnerHandle handle, bool active) {
    if (!impl_ || !impl_->isValid(handle)) return failure<void>(DiagnosticCode::StaleHandle, "Stale spawner handle");
    auto& world = *impl_;
    if (world.advancing || world.notifying)
        return failure<void>(DiagnosticCode::PreconditionViolation, "Queue controls during advance or dispatch");
    if (world.slots[handle.slot.index()].active == active) return Result<void>::success();
    if (world.pendingEvents.size() + 1 > world.limits.queuedCommands)
        return failure<void>(DiagnosticCode::PreconditionViolation,
                             "Pending event capacity exhausted; advance before more controls");
    world.pendingEvents.reserve(world.pendingEvents.size() + 1);
    auto applied = world.apply({handle, active ? ControlKind::Activate : ControlKind::Pause}, world.pendingEvents);
    if (!applied) return Result<void>::failure(applied.status());
    return Result<void>::success();
}
Result<void> SpawnWorld::enqueue(ControlCommand command) {
    if (!impl_ || !impl_->isValid(command.source))
        return failure<void>(DiagnosticCode::StaleHandle, "Stale queued source");
    if (command.kind != ControlKind::Activate && command.kind != ControlKind::Pause &&
        command.kind != ControlKind::RetryFault && command.kind != ControlKind::ReconcilePopulation)
        return failure<void>(DiagnosticCode::InvalidArgument, "Unknown spawn control");
    if (impl_->advancing || impl_->commands.size() >= impl_->limits.queuedCommands)
        return failure<void>(DiagnosticCode::PreconditionViolation, "World is advancing or control queue is full");
    impl_->commands.push_back(command);
    return Result<void>::success();
}
Result<Subscription> SpawnWorld::subscribe(Observer<SpawnEvent>::Callback callback) {
    if (!impl_ || impl_->advancing || !callback)
        return failure<Subscription>(DiagnosticCode::PreconditionViolation,
                                     "Expected a nonempty callback on an idle world");
    return Result<Subscription>::success(impl_->observers.subscribe(std::move(callback)));
}
Result<DetachReceipt> SpawnWorld::detach(SpawnerHandle handle, DetachPolicy policy) {
    if (!impl_ || !impl_->isValid(handle))
        return failure<DetachReceipt>(DiagnosticCode::StaleHandle, "Stale detach source");
    auto& world = *impl_;
    if (world.advancing || world.notifying)
        return failure<DetachReceipt>(DiagnosticCode::PreconditionViolation, "Detach requires an idle world");
    if (policy != DetachPolicy::KeepUnits && policy != DetachPolicy::RemoveUnits)
        return failure<DetachReceipt>(DiagnosticCode::InvalidArgument, "Unknown detach policy");
    if (world.pendingEvents.size() + 1 > std::numeric_limits<std::uint64_t>::max() - world.outputSequence)
        return failure<DetachReceipt>(DiagnosticCode::PreconditionViolation, "Event sequence exhausted");
    auto&          slot = world.slots[handle.slot.index()];
    RemovalRequest request;
    request.source = handle;
    request.policy = policy;
    request.operation =
        world.owner.child("spawn/epoch/" + std::to_string(world.epoch) + "/instance/" + slot.definition.id + "/detach");
    request.subjects.reserve(slot.tracked.size());
    for (const auto& tracked : slot.tracked) request.subjects.push_back(tracked.subject);
    DetachReceipt receipt;
    receipt.released          = request.subjects.size();
    receipt.cancelledCommands = static_cast<std::size_t>(std::count_if(
        world.commands.begin(), world.commands.end(), [&](const auto& command) { return command.source == handle; }));
    receipt.events            = world.pendingEvents;
    receipt.events.reserve(receipt.events.size() + 1);
    world.emit(receipt.events, handle.slot.index(), SpawnEventKind::Detached, slot.wave);
    if (!request.subjects.empty()) {
        auto providers = world.providers.lock();
        auto factory   = providers ? providers->factory : nullptr;
        if (!factory)
            return failure<DetachReceipt>(DiagnosticCode::NotFound, "Cleanup provider absent; source remains tracked");
        world.advancing = true;
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{world.advancing};
        auto removed = invokeProvider([&] { return factory->remove(request); });
        if (!removed) return Result<DetachReceipt>::failure(removed.status());
        if (removed.value().completed != request.subjects.size())
            return failure<DetachReceipt>(DiagnosticCode::InvariantViolation,
                                          "Cleanup receipt does not cover complete batch");
    }
    for (const auto& tracked : slot.tracked) world.subjects.erase(tracked.subject.persistentId());
    world.budgetAlive[slot.definition.budgetIndex] -= slot.alive;
    slot.tracked.clear();
    slot.replacements.clear();
    slot.pending.reset();
    slot.alive    = 0;
    slot.occupied = false;
    slot.marker.reset();
    const auto generation = RuntimeHandle<SpawnerTag>::nextGeneration(slot.generation);
    if (generation) slot.generation = *generation;
    std::erase_if(world.commands, [&](const auto& command) { return command.source == handle; });
    world.pendingEvents.clear();
    world.dispatch(receipt.events, receipt.diagnostics);
    return Result<DetachReceipt>::success(std::move(receipt));
}
Result<SpawnerSnapshot> SpawnWorld::inspect(SpawnerHandle handle) const {
    if (!impl_ || !impl_->isValid(handle))
        return failure<SpawnerSnapshot>(DiagnosticCode::StaleHandle, "Stale spawner handle");
    if (impl_->advancing)
        return failure<SpawnerSnapshot>(DiagnosticCode::PreconditionViolation, "Cannot inspect during publication");
    const auto&     slot = impl_->slots[handle.slot.index()];
    SpawnerSnapshot snapshot;
    snapshot.id      = slot.definition.id;
    snapshot.phase   = slot.phase;
    snapshot.blocked = slot.blocked;
    snapshot.alive   = slot.alive;
    snapshot.total   = slot.total;
    snapshot.wave    = slot.wave;
    auto due         = slot.due;
    if (!slot.replacements.empty()) due = std::max(due, slot.replacements.front());
    snapshot.remaining = Duration(std::max(std::int64_t(0), due - impl_->age(slot)));
    for (const auto& tracked : slot.tracked) snapshot.subjects.push_back(tracked.subject);
    return Result<SpawnerSnapshot>::success(std::move(snapshot));
}

Result<WorldDiagnostics> SpawnWorld::captureDiagnostics() const {
    if (!impl_ || !impl_->attached || impl_->advancing)
        return failure<WorldDiagnostics>(DiagnosticCode::PreconditionViolation,
                                         "Diagnostics require a published attached world");
    const auto&      world = *impl_;
    WorldDiagnostics snapshot;
    snapshot.owner       = world.owner;
    snapshot.epoch       = world.epoch;
    snapshot.incarnation = world.incarnation;
    snapshot.tick        = SimulationTick(world.tick);
    snapshot.sequence    = EventSequence(world.outputSequence);
    snapshot.budgets.reserve(world.budgets.size());
    for (std::size_t i = 0; i < world.budgets.size(); ++i)
        snapshot.budgets.push_back({world.budgets[i].id, world.budgetAlive[i], world.budgets[i].alive});
    snapshot.sources.reserve(world.slots.size());
    for (std::size_t i = 0; i < world.slots.size(); ++i) {
        const auto& slot = world.slots[i];
        if (!slot.occupied) continue;
        const SpawnerHandle source{RuntimeHandle<SpawnerTag>(static_cast<std::uint32_t>(i), slot.generation),
                                   world.owner, world.epoch, world.incarnation};
        auto                state = inspect(source);
        if (!state) return Result<WorldDiagnostics>::failure(state.status());
        snapshot.sources.push_back({source, std::move(state).takeValue(), world.budgets[slot.definition.budgetIndex].id,
                                    slot.definition.revision->definition().limits.alive, slot.definition.revision,
                                    slot.definition.activation});
    }
    return Result<WorldDiagnostics>::success(std::move(snapshot));
}

std::uint32_t  SpawnWorld::activationInputLimit() const noexcept { return impl_ ? impl_->limits.queuedCommands : 0; }
SimulationTick SpawnWorld::lastConsumedTick() const noexcept { return SimulationTick(impl_ ? impl_->tick : 0); }

Result<AdvanceReport> SpawnWorld::advance(const SimulationStep& step, std::span<const LifecycleInput> lifecycle,
                                          ActivationFrame activation) {
    if (!impl_ || !impl_->attached || impl_->advancing || impl_->notifying)
        return failure<AdvanceReport>(DiagnosticCode::PreconditionViolation, "World is detached or advancing");
    auto& world = *impl_;
    if (step.tick.value() <= world.tick || step.delta.nanoseconds() < 0 ||
        step.delta.nanoseconds() > std::numeric_limits<std::int64_t>::max() - world.now)
        return failure<AdvanceReport>(DiagnosticCode::InvalidArgument, "Invalid tick or duration");
    auto sequence = world.sequence;
    for (const auto& input : lifecycle) {
        if (!world.isValid(input.source))
            return failure<AdvanceReport>(DiagnosticCode::StaleHandle,
                                          "Lifecycle source belongs to a stale world or spawner");
        const auto existing = world.subjects.find(input.subject.persistentId());
        if (existing != world.subjects.end() && existing->second.first != input.source.slot.index())
            return failure<AdvanceReport>(DiagnosticCode::InvalidArgument,
                                          "Lifecycle source does not own this subject");
        if (!input.subject.isValid() || input.sequence == 0 ||
            (input.sequence > world.sequence && input.sequence <= sequence))
            return failure<AdvanceReport>(DiagnosticCode::InvalidArgument,
                                          "Lifecycle events must have valid identity and ordered sequence");
        sequence = std::max(sequence, input.sequence);
    }
    const auto visitCount = std::min<std::size_t>(world.slots.size(), world.limits.instancesPerTick);
    if ((activation.players && activation.players->size() > world.limits.queuedCommands) ||
        activation.events.size() > world.limits.queuedCommands)
        return failure<AdvanceReport>(DiagnosticCode::InvalidArgument, "Activation input exceeds work cap");
    if (activation.players)
        for (auto point : *activation.players)
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return failure<AdvanceReport>(DiagnosticCode::InvalidArgument, "Nonfinite player position");
    std::set<std::uint32_t>     eventSources;
    std::vector<ControlCommand> activationCommands;
    activationCommands.reserve(visitCount + activation.events.size());
    for (auto source : activation.events) {
        if (!world.isValid(source))
            return failure<AdvanceReport>(DiagnosticCode::StaleHandle, "Activation event source is stale");
        if (!std::holds_alternative<EventActivation>(world.slots[source.slot.index()].definition.activation) ||
            !eventSources.insert(source.slot.index()).second)
            return failure<AdvanceReport>(DiagnosticCode::InvalidArgument, "Invalid or duplicate activation event");
        activationCommands.push_back({source, ControlKind::Activate});
    }
    std::map<std::uint32_t, bool> queuedActivation;
    for (const auto& command : world.commands)
        if (command.kind == ControlKind::Activate || command.kind == ControlKind::Pause)
            queuedActivation[command.source.slot.index()] = command.kind == ControlKind::Activate;
    if (activation.players)
        for (std::size_t visited = 0; visited < visitCount; ++visited) {
            const auto  index = (world.cursor + visited) % world.slots.size();
            const auto& slot  = world.slots[index];
            if (!slot.occupied) continue;
            const auto* proximity = std::get_if<ProximityActivation>(&slot.definition.activation);
            if (!proximity) continue;
            double nearest = std::numeric_limits<double>::infinity();
            for (auto point : *activation.players) {
                auto distance = detail::distanceToValidatedRegion(slot.definition.region, point);
                if (!distance) return Result<AdvanceReport>::failure(distance.status());
                nearest = std::min(nearest, distance.value());
            }
            const auto queued           = queuedActivation.find(static_cast<std::uint32_t>(index));
            const bool previouslyActive = queued == queuedActivation.end() ? slot.active : queued->second;
            const bool active =
                previouslyActive ? nearest <= proximity->exitDistance : nearest <= proximity->enterDistance;
            activationCommands.push_back({world.handle(index), active ? ControlKind::Activate : ControlKind::Pause});
        }
    const auto maximumEvents = visitCount * 6 + lifecycle.size() * 2 + world.commands.size() * 5 +
                               activationCommands.size() * 2 + world.pendingEvents.size() + world.limits.batchesPerTick;
    if (maximumEvents > std::numeric_limits<std::uint64_t>::max() - world.outputSequence)
        return failure<AdvanceReport>(DiagnosticCode::PreconditionViolation, "Event sequence exhausted");
    AdvanceReport report;
    report.events.reserve(maximumEvents);
    for (auto& event : world.pendingEvents) report.events.push_back(std::move(event));
    world.pendingEvents.clear();
    world.advancing = true;
    struct Guard {
        bool& advancing;
        ~Guard() { advancing = false; }
    } guard{world.advancing};
    world.now += step.delta.nanoseconds();
    world.tick    = step.tick.value();
    auto commands = std::exchange(world.commands, {});
    for (const auto& command : commands) {
        auto applied = world.apply(command, report.events);
        if (!applied) report.diagnostics.push_back(failureDiagnostic(applied.status()));
    }
    for (const auto& command : activationCommands) {
        auto applied = world.apply(command, report.events);
        if (!applied) report.diagnostics.push_back(failureDiagnostic(applied.status()));
    }
    for (const auto& input : lifecycle) {
        if (input.sequence <= world.sequence) continue;
        const auto found = world.subjects.find(input.subject.persistentId());
        if (found == world.subjects.end()) continue;
        Impl::Transition transition{world, report.events, found->second.first};
        auto&            slot      = world.slots[found->second.first];
        const auto       unitIndex = found->second.second;
        if (unitIndex != slot.tracked.size() - 1) {
            slot.tracked[unitIndex]                                                  = slot.tracked.back();
            world.subjects.at(slot.tracked[unitIndex].subject.persistentId()).second = unitIndex;
        }
        slot.tracked.pop_back();
        world.subjects.erase(found);
        --slot.alive;
        --world.budgetAlive[slot.definition.budgetIndex];
        if (const auto* population = std::get_if<PopulationSchedule>(&slot.definition.revision->definition().schedule);
            population && input.reason == RemovalReason::Death) {
            world.scheduleReplacement(slot, *population);
            std::sort(slot.replacements.begin(), slot.replacements.end());
        }
        if (std::holds_alternative<WaveSchedule>(slot.definition.revision->definition().schedule) &&
            input.reason != RemovalReason::Death) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::UnexpectedRemoval;
        }
    }
    world.sequence                  = sequence;
    auto          providers         = world.providers.lock();
    auto          placementProvider = providers ? providers->placement : nullptr;
    auto          factoryProvider   = providers ? providers->factory : nullptr;
    std::uint64_t candidates        = 0;
    const auto    start             = world.cursor;
    auto          nextCursor        = start;
    for (std::size_t visited = 0; visited < visitCount; ++visited) {
        const auto index = (start + visited) % world.slots.size();
        auto&      slot  = world.slots[index];
        ++report.visited;
        if (!slot.occupied) continue;
        Impl::Transition transition{world, report.events, index};
        if (!slot.announced) {
            slot.announced = true;
            if (slot.phase == Phase::Running) world.emit(report.events, index, SpawnEventKind::Activated, slot.wave);
        }
        if (slot.phase != Phase::Running) continue;
        const auto& definition = slot.definition.revision->definition();
        const auto  age        = world.age(slot);
        const auto* waves      = std::get_if<WaveSchedule>(&definition.schedule);
        if (waves && slot.wavePublished && slot.alive == 0) {
            slot.wavePublished = false;
            ++slot.wave;
            if (slot.wave == waves->waves.size()) {
                slot.phase = Phase::Completed;
                continue;
            }
            slot.due = world.deadline(slot, age, waves->waves[slot.wave].delay);
        }
        if (waves && slot.wavePublished) continue;
        if (age < slot.due) continue;
        std::uint32_t              count      = 0;
        const std::vector<Member>* fixed      = nullptr;
        const auto*                interval   = std::get_if<IntervalSchedule>(&definition.schedule);
        const auto*                population = std::get_if<PopulationSchedule>(&definition.schedule);
        if (waves) {
            fixed = &waves->waves[slot.wave].members;
            count = groupSize(*fixed);
        } else if (std::holds_alternative<OnceSchedule>(definition.schedule)) {
            fixed = &std::get<FixedContent>(definition.content).members;
            count = groupSize(*fixed);
        } else if (interval)
            count = interval->count;
        else if (!slot.initialDone)
            count = population->initial;
        else if (!slot.replacements.empty() && slot.replacements.front() <= age)
            count = 1;
        if (count == 0) {
            if (population && !slot.initialDone) world.finishInitialPopulation(slot, *population);
            slot.initialDone = true;
            continue;
        }
        if (definition.limits.total &&
            (slot.total >= *definition.limits.total || count > *definition.limits.total - slot.total)) {
            slot.phase = Phase::Completed;
            continue;
        }
        const auto attempts = static_cast<std::uint64_t>(count) * definition.placement.attempts;
        if (report.batches >= world.limits.batchesPerTick || count > world.limits.unitsPerTick - report.units ||
            attempts > world.limits.candidatesPerTick - candidates)
            continue;
        auto retry = [&](BlockReason reason) {
            slot.blocked = reason;
            if (interval) {
                // Missed periods are skipped without multiplying into overflow.
                const auto period = interval->period.nanoseconds();
                slot.due          = world.deadline(slot, age, Duration(period - (age - slot.due) % period));
                slot.pending.reset();
            } else
                slot.due = world.deadline(slot, age, definition.retryDelay);
        };
        const auto budgetIndex = slot.definition.budgetIndex;
        if (count > definition.limits.alive - slot.alive ||
            count > world.budgets[budgetIndex].alive - world.budgetAlive[budgetIndex]) {
            retry(BlockReason::BudgetFull);
            continue;
        }
        if (!placementProvider || !factoryProvider) {
            retry(BlockReason::ProviderUnavailable);
            continue;
        }
        ++report.batches;
        candidates += attempts;
        if ((!slot.pending && slot.operation == std::numeric_limits<std::uint64_t>::max()) ||
            count > std::numeric_limits<std::uint64_t>::max() - slot.total) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::ArithmeticOverflow;
            continue;
        }
        if (!slot.pending) {
            slot.pending         = world.batch(slot, count, fixed);
            slot.pending->source = world.handle(index);
        }
        auto planned = invokeProvider([&] {
            return placementProvider->plan(*slot.pending, definition.placement, slot.definition.region,
                                           slot.placementRandom);
        });
        if (!planned) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::PlacementBlocked;
            report.diagnostics.push_back(failureDiagnostic(planned.status()));
            continue;
        }
        if (std::holds_alternative<PlacementBlocked>(planned.value())) {
            retry(BlockReason::PlacementBlocked);
            continue;
        }
        const auto& positions = std::get<std::vector<Point>>(planned.value());
        if (positions.size() != count || std::any_of(positions.begin(), positions.end(), [](Point p) {
                return !std::isfinite(p.x) || !std::isfinite(p.y);
            })) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::PlacementBlocked;
            report.diagnostics.push_back(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "Placement returned invalid positions"));
            continue;
        }
        auto prepared = invokeProvider([&] { return factoryProvider->prepare(*slot.pending, positions); });
        if (!prepared) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::FactoryFailed;
            report.diagnostics.push_back(failureDiagnostic(prepared.status()));
            continue;
        }
        if (std::holds_alternative<PlacementBlocked>(prepared.value())) {
            retry(BlockReason::PlacementBlocked);
            continue;
        }
        auto& transaction = std::get<std::unique_ptr<IPreparedSpawn>>(prepared.value());
        if (!transaction) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::FactoryFailed;
            report.diagnostics.push_back(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "Null prepared transaction"));
            continue;
        }
        // Allocate bookkeeping before publication; callbacks cannot observe this private staging.
        slot.tracked.reserve(slot.tracked.size() + count);
        if (population && !slot.initialDone) slot.replacements.reserve(population->target);
        std::map<PersistentId, std::pair<std::size_t, std::size_t>> staged;
        bool                                                        identitiesValid = true;
        for (std::size_t n = 0; n < count; ++n) {
            const auto id = slot.pending->units[n].subject.persistentId();
            if (world.subjects.contains(id) || !staged.emplace(id, std::pair{index, slot.tracked.size() + n}).second) {
                identitiesValid = false;
                break;
            }
        }
        if (!identitiesValid) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::FactoryFailed;
            report.diagnostics.push_back(
                Diagnostic::error(DiagnosticCode::Conflict, "Generated subject identity collision", "spawn.identity"));
            continue;
        }
        SpawnEvent published;
        published.tick      = step.tick;
        published.source    = world.handle(index);
        published.kind      = SpawnEventKind::BatchCommitted;
        published.operation = slot.pending->operation;
        published.wave      = slot.wave;
        published.subjects.reserve(count);
        for (const auto& unit : slot.pending->units) published.subjects.push_back(unit.subject);
        auto committed = invokeProvider([&] { return transaction->commit(); });
        if (!committed) {
            slot.phase   = Phase::Faulted;
            slot.blocked = BlockReason::FactoryFailed;
            report.diagnostics.push_back(failureDiagnostic(committed.status()));
            continue;
        }
        if (committed.value() == CommitOutcome::PlacementBlocked) {
            retry(BlockReason::PlacementBlocked);
            continue;
        }

        for (const auto& unit : slot.pending->units) slot.tracked.push_back({unit.subject, slot.wave});
        world.subjects.merge(staged);
        slot.total += count;
        slot.alive += count;
        world.budgetAlive[budgetIndex] += count;
        report.units += count;
        report.events.push_back(std::move(published));
        nextCursor = (index + 1) % world.slots.size();
        slot.pending.reset();
        slot.blocked = BlockReason::None;
        if (waves)
            slot.wavePublished = true;
        else if (interval)
            slot.due = world.deadline(
                slot, age,
                Duration(interval->period.nanoseconds() - (age - slot.due) % interval->period.nanoseconds()));
        else if (population) {
            if (!slot.initialDone)
                world.finishInitialPopulation(slot, *population);
            else
                slot.replacements.erase(slot.replacements.begin());
            slot.due = age;
        } else
            slot.phase = Phase::Completed;
    }
    if (!world.slots.empty()) world.cursor = report.units != 0 ? nextCursor : (start + visitCount) % world.slots.size();
    world.advancing = false;
    world.dispatch(report.events, report.diagnostics);
    return Result<AdvanceReport>::success(std::move(report));
}
}  // namespace eve::spawn

#include "spawn/SpawnSnapshotCapture.inl"
#include "spawn/SpawnSnapshotRead.inl"
