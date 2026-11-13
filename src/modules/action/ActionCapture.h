#pragma once
/** @file ActionCapture.h
 * @brief action module-interface: detached executions and prepared ECS rebind for coordinated checkpoints.
 * @remarks Persistent data face eve.action-runtime / action:runtime v1; no service capture, events, script/tool
 * bindings or resource-ledger restoration.
 */
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include "action/ActionIdentity.h"
#include "common/ECS.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
namespace eve::action {
class ActionExecution;
class ActionRuntime;
/** @brief Domain-authoritative correspondence. Historical handles are values only; never dereference them.
 * @remarks Current live references are validated against the supplied actual Table. Domain owns identity truth.
 */
struct ActionEntityBinding {
    ecs::EntityHandle entity;
    SubjectRef        subject;
};
/** @brief Captured incarnation presence; a retired reference never adopts a replacement of the same subject. */
enum class ActionEntityPresence { Present, Retired };
/** @brief Active dispatch path that future uncommitted executions must retain after rebinding services. */
enum class ActionActivePipeline { LegacyPorts, Transaction };
/** @brief Stable identity and actual incarnation presence, with no ECS handle or lifetime token. */
struct CapturedActionReference {
    SubjectRef           subject;
    ActionEntityPresence presence = ActionEntityPresence::Retired;
};
/** @brief Handle-free read-only execution facts, including private phase/cursor/commit state in the owned copy.
 * @remarks request().source/targetEntities are cleared; these correspondences replace them. Stable producer
 * metadata, resolved TargetSet, receipt audits and timing overrides remain owned. Not directly resubmittable.
 */
struct CapturedActionExecution {
    std::shared_ptr<const ActionExecution> execution;
    std::optional<CapturedActionReference> source;
    std::vector<CapturedActionReference>   targets;
};
/** @brief Immutable move-only freeze of all executions and id allocation watermark.
 * @ownership Owns definitions, requests, resolved targets, receipts and cursors; no Table, service or callback.
 * @thread Capture on runtime owner thread. Source retirement cannot invalidate detached facts.
 * @remarks Audit account nonces/execution ids are in-process values, not durable account identities. The game
 * coordinator must join ledgers, adapter correlations, clocks and domain state before persistent publication.
 */
class EVENGINE_API_PLATFORM ActionCapture final {
public:
    /** @brief Release detached facts. */
    ~ActionCapture();
    /** @brief Transfer detached facts. */
    ActionCapture(ActionCapture&&) noexcept;
    /** @brief Replace detached facts by move. */
    ActionCapture& operator=(ActionCapture&&) noexcept;
    ActionCapture(const ActionCapture&)            = delete;
    ActionCapture& operator=(const ActionCapture&) = delete;
    /** @brief Joint save identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Original Active dispatch path, or empty after move; provider implementation identity is external. */
    [[nodiscard]] std::optional<ActionActivePipeline> activePipeline() const noexcept;
    /** @brief Exact next execution id, or zero after move. */
    [[nodiscard]] ActionExecutionId nextExecutionId() const noexcept;
    /** @brief Borrow id-ordered immutable executions until move/destruction. */
    [[nodiscard]] std::span<const CapturedActionExecution> executions() const noexcept;

    /** @brief Seal complete detached executions as eve.action-runtime / action:runtime v1.
     * @param revision Nonzero joint-save revision; tick is the externally coordinated frame boundary.
     * @param hash Synchronous digest provider; never retained.
     * @return Owning versioned envelope; no services, credentials or world references.
     * @thread Caller synchronizes moves; immutable CPU encoding, no simulation advance.
     * @cost Copies all execution/definition/target/receipt data once per explicit save.
     * @throws std::bad_alloc Source and live worlds remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Admit strict v1 fields, ordered ids, phase/cursor facts and historical audits without execution.
     * @param snapshot Owned envelope; epoch must match its nonnil instance identity.
     * @param hash Synchronous digest verifier; never retained.
     * @return Detached complete facts; initial v1 has no implicit migration or unknown fields.
     * @remarks No account nonce/credential is recreated; provider identity, ledgers, clocks and domain
     * correspondence still require joint admission and PreparedActionRestore binding before publication.
     * @thread Pure CPU admission; no providers, time, RNG or effects are invoked.
     * @cost Validates and copies every nested execution once per explicit load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<ActionCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                     const SnapshotHashProvider& hash);

private:
    friend class ActionRuntime;
    friend class PreparedActionRestore;
    struct Impl;
    explicit ActionCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Fully copied execution candidate, with current target bindings and weak actual Table ownership.
 * @ownership Never retains services or a source owner. Target Table retirement makes publication fail safely.
 * @thread Prepare/commit on simulation owner thread outside Views; no service callbacks or simulation advance.
 */
class EVENGINE_API_PLATFORM PreparedActionRestore final {
public:
    /** @brief Release candidate only. */
    ~PreparedActionRestore();
    /** @brief Transfer candidate. */
    PreparedActionRestore(PreparedActionRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedActionRestore& operator=(PreparedActionRestore&&) noexcept;
    PreparedActionRestore(const PreparedActionRestore&)            = delete;
    PreparedActionRestore& operator=(const PreparedActionRestore&) = delete;
    /** @brief Clone frozen executions and rebind only references that were present; retired links stay invalid.
     * @return Unpublished candidate or missing/ambiguous/current-world identity failure; no runtime is changed.
     * @remarks Domain coordinator validates identities before supplying bindings. No reservations/effects execute.
     * @cost Copies all frozen execution data and resolves referenced identities per restore attempt.
     * @throws std::bad_alloc Candidate allocation failed without input or target mutation.
     */
    [[nodiscard]] static Result<PreparedActionRestore> prepare(const ActionCapture&                 captured,
                                                               std::shared_ptr<ecs::Table>          world,
                                                               std::span<const ActionEntityBinding> bindings);

private:
    friend class ActionRuntime;
    struct Impl;
    explicit PreparedActionRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::action
