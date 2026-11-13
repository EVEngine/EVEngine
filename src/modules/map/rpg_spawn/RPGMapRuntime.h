#pragma once
/** @file RPGMapRuntime.h
 * @brief map_rpg_spawn module-interface: owned RPG/map frames, acknowledged event delivery and joint persistence.
 * @remarks Direct composition only: no new ECS root, capability, script or tool face. Canonical modules own domain
 * facts.
 */
#include <memory>
#include <optional>
#include <string>
#include "common/Export.h"
#include "common/Snapshot.h"
#include "spawn/SpawnWorld.h"
namespace eve::graphics {
class ISolidRectRenderer;
}
namespace eve::service {
class IFileSystem;
}
namespace eve::rpg {
class SkillConditionBindings;
}
namespace eve::rpg::detail {
struct SimulationEvents;
}
namespace eve::rpg_spawn {
struct ActorSnapshot;
}
namespace eve::map_rpg_spawn {
struct ActorStyle;
class RPGMapRuntime;
class PreparedRPGRuntimeRestore;
/** @brief Prepared owning player/event facts, bound to one native runtime incarnation.
 * @ownership Copies/moves input values; retains no runtime, Table or provider. Reusable while its owner is active.
 * @thread Simulation owner thread. Pending frames freeze their own facts; closing/restoring invalidates old tokens.
 */
class EVENGINE_API_WORLD RPGActivation {
public:
    /** @brief Release local facts. */
    ~RPGActivation();
    /** @brief Transfer local facts. */
    RPGActivation(RPGActivation&&) noexcept;
    /** @brief Replace local facts. */
    RPGActivation& operator=(RPGActivation&&) noexcept;
    RPGActivation(const RPGActivation&)            = delete;
    RPGActivation& operator=(const RPGActivation&) = delete;

private:
    friend class RPGMapRuntime;
    friend class PreparedRPGRuntimeRestore;
    struct Impl;
    explicit RPGActivation(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};
/** @brief A copied five-queue delivery and opaque prefix receipt; reading never consumes events.
 * @ownership Queue values are independent, actor references remain weak. The receipt belongs to one runtime
 * incarnation.
 * @thread Owner thread; ack after side effects only. External side effects require their own idempotent transaction.
 */
class EVENGINE_API_WORLD RPGEventDelivery {
public:
    /** @brief Release copied queue facts and receipt. */
    ~RPGEventDelivery();
    /** @brief Transfer copied facts and receipt. */
    RPGEventDelivery(RPGEventDelivery&&) noexcept;
    /** @brief Replace copied facts and receipt. */
    RPGEventDelivery& operator=(RPGEventDelivery&&) noexcept;
    RPGEventDelivery(const RPGEventDelivery&)            = delete;
    RPGEventDelivery& operator=(const RPGEventDelivery&) = delete;
    /** @brief Borrow copied facts until destruction/move; moved-from delivery observes empty facts. */
    [[nodiscard]] const rpg::detail::SimulationEvents& events() const noexcept;

private:
    friend class RPGMapRuntime;
    struct Impl;
    explicit RPGEventDelivery(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/** @brief Detached same-boundary map/actor/event/frame checkpoint; source runtime may be destroyed.
 * @ownership Owns frozen canonical participants and delivery watermarks, never source worlds or callbacks.
 * @thread Content owner thread; same-state sealing reentry rejects. No locks across digest callbacks.
 */
class EVENGINE_API_WORLD RPGRuntimeCheckpoint {
public:
    /** @brief Release frozen participants. */
    ~RPGRuntimeCheckpoint();
    /** @brief Transfer frozen participants. */
    RPGRuntimeCheckpoint(RPGRuntimeCheckpoint&&) noexcept;
    /** @brief Replace frozen participants. */
    RPGRuntimeCheckpoint& operator=(RPGRuntimeCheckpoint&&) noexcept;
    RPGRuntimeCheckpoint(const RPGRuntimeCheckpoint&)            = delete;
    RPGRuntimeCheckpoint& operator=(const RPGRuntimeCheckpoint&) = delete;
    /** @brief Seal eve.map-rpg-runtime / map:rpg-runtime v2 with strict canonical nested participants.
     * @return Owning envelope or checked failure retaining facts for retry; no implicit legacy migration.
     * @reentrancy Pins state before callbacks; moving/destroying the checkpoint in a digest callback is supported.
     * @cost Whole participant encoding and hashing per explicit file save.
     * @throws std::bad_alloc Live and frozen facts remain unchanged. */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hash) const;

private:
    friend class RPGMapRuntime;
    struct Impl;
    explicit RPGRuntimeCheckpoint(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};
/** @brief Private Map, actor Table, content and canonical queues with explicit contiguous simulation frames.
 * @ownership Owns complete private state; event projections carry weak actor references. Call close for observable
 * cleanup.
 * @thread Simulation owner thread outside Views; non-reentrant; callers must not destroy the runtime inside callbacks.
 * @remarks Status then skill systems run exactly once per frame. A pre-consumption spawn failure keeps that exact
 * frame pending; retry never repeats domain time. Partial domain failure or post-consumption exception faults the
 * owner. No implicit wall clock/RNG exists. Gameplay callbacks retain their canonical restrictions; full Battle is
 * unsupported.
 */
class EVENGINE_API_WORLD RPGMapRuntime {
public:
    /** @brief Release the private world; does not call gameplay cleanup callbacks. */
    ~RPGMapRuntime();
    RPGMapRuntime(const RPGMapRuntime&)            = delete;
    RPGMapRuntime& operator=(const RPGMapRuntime&) = delete;
    /** @brief Prepare an isolated map/session using owned unit/template/binding copies and canonical admission.
     * @return Owning runtime, or failure with no caller-world mutation. No actors are created before first advance.
     * @ownership Files is borrowed synchronously; no input/provider pointer is retained. Players start known empty.
     * @cost Whole content/map admission per new game; amortized over explicit frames.
     * @throws std::bad_alloc No runtime is published. */
    [[nodiscard]] static Result<std::unique_ptr<RPGMapRuntime>> prepare(
        std::vector<Value> units, std::vector<Value> templates, Value binding, service::IFileSystem& files,
        PersistentId owner, std::uint64_t seed, double bodyRadius,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Run canonical RPG systems once, retain lifecycle facts, then advance spawning.
     * @return Report or failure. Ready accepts tick completed+1 and positive delta; pending requires exact same step.
     * @remarks Ready without a token supplies known-empty players/no events; a pending retry uses its frozen input.
     * @cost Actual domain traversal plus bounded spawn work per injected frame. */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(const SimulationStep& step);
    /** @brief Prepare owned activation facts and bind event names to this world's exact event-configured sources.
     * @param players Absent preserves previous player facts; present empty is known-no-players and pauses proximity.
     * @param events Named external events; duplicates/unknown names reject before simulation. Both bounds are 1024.
     * @return Immutable token or phase/nonfinite/unknown-event failure without mutation or callbacks.
     * @cost Linear player validation; event inputs additionally copy/scan source and membership diagnostics once per
     * preparation.
     * @throws std::bad_alloc Live world and caller input remain independent. */
    [[nodiscard]] Result<RPGActivation> prepareActivation(std::optional<std::vector<spawn::Point>> players,
                                                          std::vector<std::string>                 events) const;
    /** @brief Advance with prebound activation; a pending retry must carry identical frozen facts.
     * @return Report or checked stale/changed-input failure before further domain work.
     * @cost Domain traversal and bounded spawn work per frame; no event-name or service discovery. */
    [[nodiscard]] Result<spawn::AdvanceReport> advance(const SimulationStep& step, const RPGActivation& activation);
    /** @brief Apply checked HP damage and retain resulting lifecycle without advancing either clock.
     * @return Applied damage or failure. A handoff allocation exception may follow committed damage; retry advance,
     * not damage. Queue facts remain owned. No gameplay callback/Map borrow escapes.
     * @cost Canonical resource preparation plus tracked lifecycle reconciliation per explicit damage action. */
    [[nodiscard]] Result<double> applyDamage(SubjectRef subject, double amount);
    /** @brief Observe one owned actor without mutation or callbacks.
     * @cost Logarithmic source lookup per explicit inspection. */
    [[nodiscard]] Result<rpg_spawn::ActorSnapshot> inspectActor(SubjectRef subject) const;
    /** @brief Copy all pending canonical event queues and an opaque prefix receipt without consuming them.
     * @return Independent delivery or checked phase/counter failure; no callbacks or mutation.
     * @cost Copy every pending fact per consumer observation. */
    [[nodiscard]] Result<RPGEventDelivery> readEvents() const;
    /** @brief Confirm exactly a previously read prefix, preserving events appended after its capture.
     * @return Applied, or stale/conflict without consuming anything. Empty/duplicate/out-of-order/foreign receipts
     * reject.
     * @remarks Resource prefix must already be accepted by lifecycle handoff; no allocations or callbacks on success.
     * @cost Linear no-throw prefix erasure per consumer acknowledgement. */
    [[nodiscard]] Result<void> acknowledge(const RPGEventDelivery& delivery);
    /** @brief Copy phase, completed tick and source diagnostics; no simulation advancement.
     * @remarks Sources include actual phase/wave/wait/alive/subjects; total is an exact decimal string.
     * @cost Linear scheduler diagnostic copy per observation. */
    [[nodiscard]] Result<Value> captureState() const;
    /** @brief Draw captured live actor markers through the canonical map presentation boundary.
     * @ownership Renderer/style borrowed only for this call; no world pointers escape.
     * @thread Simulation/render owner thread in Ready; renderer reentry into this runtime rejects.
     * @remarks No locks or Views across renderer callbacks. Renderer exceptions propagate after possible draws.
     * @cost Linear actor/source observation and temporary draw-batch allocation per presentation.
     */
    [[nodiscard]] Result<std::size_t> draw(graphics::ISolidRectRenderer& renderer, const ActorStyle& style) const;
    /** @brief Freeze map/core/actor state, all queues, phase and independent delivery/lifecycle watermarks.
     * @return Detached facts or explicit identity/content/phase failure without consuming events.
     * @remarks Ready and SpawnPending only. Unsupported content outside the canonical closure and unversioned global
     * gameplay callbacks reject.
     * @cost Whole world and queue capture per explicit builder; reuse its seal for file retries.
     * @throws std::bad_alloc No live state changes. */
    [[nodiscard]] Result<RPGRuntimeCheckpoint> captureCheckpoint(PersistentId epoch, Revision revision);
    /** @brief Remove managed sources and close; failure preserves cleanup responsibility.
     * @cost Linear source cleanup per unload attempt; no simulation advance. */
    [[nodiscard]] Result<void> close();

private:
    friend class PreparedRPGRuntimeRestore;
    struct Impl;
    explicit RPGMapRuntime(std::unique_ptr<Impl> impl);
    [[nodiscard]] Result<spawn::AdvanceReport> advanceBound(const SimulationStep& step,
                                                            const RPGActivation*  activation);
    std::unique_ptr<Impl>                      impl_;
};
/** @brief CPU-admitted whole-runtime restore; commit builds an isolated world then atomically exchanges owner.
 * @ownership Owns strict same-boundary facts and immutable execution bindings, never source actors/provider callbacks.
 * @thread Simulation/content owner thread, non-reentrant. No locks or Views across external digest callbacks.
 */
class EVENGINE_API_WORLD PreparedRPGRuntimeRestore {
public:
    /** @brief Release admitted facts. */
    ~PreparedRPGRuntimeRestore();
    /** @brief Transfer admitted facts. */
    PreparedRPGRuntimeRestore(PreparedRPGRuntimeRestore&&) noexcept;
    /** @brief Replace admitted facts. */
    PreparedRPGRuntimeRestore& operator=(PreparedRPGRuntimeRestore&&) noexcept;
    PreparedRPGRuntimeRestore(const PreparedRPGRuntimeRestore&)            = delete;
    PreparedRPGRuntimeRestore& operator=(const PreparedRPGRuntimeRestore&) = delete;
    /** @brief Admit strict v2 file (or explicitly migrate known-empty v1 activation) and nested canonical participants
     * before constructing any world.
     * @return Reusable candidate or schema/boundary/content/watermark failure; unknown fields/versions reject.
     * @ownership Snapshot is owned before digest callbacks. Hash is not retained. Unknown versions reject; v1
     * activation migration is explicit.
     * @cost Whole-file verification and participant admission per restore builder.
     * @throws std::bad_alloc Existing runtime is untouched. */
    [[nodiscard]] static Result<PreparedRPGRuntimeRestore> prepare(
        SnapshotEnvelope snapshot, PersistentId epoch, const SnapshotHashProvider& hash,
        std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Materialize isolated map/actors, rebind queues, publish listeners, then exchange complete runtime owner.
     * @return Previous owner (possibly null), or failure preserving active and this candidate for retry.
     * @remarks Old native deliveries cannot acknowledge the replacement; no gameplay or hash callbacks at publication.
     * Successful commit consumes this candidate, closes the old owner and revokes old sources/receipts.
     * Retained old external observations remain old, with no implicit event replay.
     * @cost Whole candidate map/actor materialization and queue rebinding per commit attempt.
     * @throws std::bad_alloc Candidate cleanup leaves active unchanged. */
    [[nodiscard]] Result<std::unique_ptr<RPGMapRuntime>> commit(std::unique_ptr<RPGMapRuntime>& active);

private:
    struct Impl;
    explicit PreparedRPGRuntimeRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_rpg_spawn
