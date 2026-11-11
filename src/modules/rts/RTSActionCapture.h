#pragma once
/** @file RTSActionCapture.h
 * @brief rts module-interface: native action executions plus order correlations for coordinated restore.
 * @remarks Persistent data face eve.rts-actions / rts:actions v1; no ECS components, events or script/tool
 * bindings. Clocks, order bodies, providers and ledgers remain external.
 */
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
namespace ecs {
class Table;
}
namespace eve::action {
class ActionCapture;
struct ActionEntityBinding;
}  // namespace eve::action
namespace eve::rts {
class ActionAdapter;
class PreparedRTSActionRestore;
/** @brief Detached immutable joint actions/order correlations; source owners may be destroyed.
 * @ownership Owns native facts and correlation strings; retains no Table, adapter, runtime or callback.
 * @thread Capture on simulation owner thread; const inspection does not advance simulation.
 */
class EVENGINE_API_DOMAINS RTSActionCapture final {
public:
    /** @brief Release detached facts. */
    ~RTSActionCapture();
    /** @brief Transfer detached facts. */
    RTSActionCapture(RTSActionCapture&&) noexcept;
    /** @brief Replace detached facts. */
    RTSActionCapture& operator=(RTSActionCapture&&) noexcept;
    RTSActionCapture(const RTSActionCapture&)            = delete;
    RTSActionCapture& operator=(const RTSActionCapture&) = delete;
    /** @brief Borrow immutable native actions until move/destruction; empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const action::ActionCapture>> actions() const noexcept;
    /** @brief Number of retained order correlations, including retired sources. */
    [[nodiscard]] std::size_t pendingCount() const noexcept;

    /** @brief Seal complete native actions and exact pending-order correspondences as rts:actions v1.
     * @param revision Nonzero joint-save revision; tick is the caller-coordinated boundary.
     * @param hash Synchronous digest provider; never retained.
     * @return Owning envelope with nested same-boundary action:runtime facts and no live handles.
     * @thread Immutable CPU encoding; caller synchronizes moves. No simulation advance or providers.
     * @cost Copies complete executions and pending correlations once per explicit joint save.
     * @throws std::bad_alloc Source and live worlds remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Admit strict v1 ordered correlations against the canonical nested action participant.
     * @param snapshot Owned envelope; epoch must match the nonnil nested and outer identities.
     * @param hash Synchronous verifier; never retained.
     * @return Detached facts or malformed/unknown fields, versions, boundaries or inconsistent references.
     * @remarks Initial v1 has no implicit migration. Does not recreate Units, submit actions, adopt retired
     * sources, or authorize providers. Joint restore must also admit actual order bodies, clocks and ledgers.
     * @thread Pure CPU admission; no callbacks except the digest provider, no simulation mutation.
     * @cost Verifies and copies complete nested execution and correlation facts per explicit load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<RTSActionCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                        const SnapshotHashProvider& hash);

private:
    friend class ActionAdapter;
    friend class PreparedRTSActionRestore;
    struct Impl;
    explicit RTSActionCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Unpublished native actions and current order bindings, weakly observing the target world.
 * @thread Prepare and publish on simulation owner thread outside Views; no effects or reservations execute.
 */
class EVENGINE_API_DOMAINS PreparedRTSActionRestore final {
public:
    /** @brief Release candidate only. */
    ~PreparedRTSActionRestore();
    /** @brief Transfer candidate. */
    PreparedRTSActionRestore(PreparedRTSActionRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedRTSActionRestore& operator=(PreparedRTSActionRestore&&) noexcept;
    PreparedRTSActionRestore(const PreparedRTSActionRestore&)            = delete;
    PreparedRTSActionRestore& operator=(const PreparedRTSActionRestore&) = delete;
    /** @brief Prepare native executions and bind only originally present sources to actual RTS Units.
     * @ownership Output owns candidate data, weakly observes world; mappings are borrowed during prepare.
     * @return Candidate or missing/stale/conflicting identity; leaves the source and target untouched.
     * @remarks Coordinator must separately join unit orders, clocks, service implementations and ledgers.
     * @cost Copies native execution data and resolves correlations per restore attempt.
     * @throws std::bad_alloc Preparation failed without publishing state.
     */
    [[nodiscard]] static Result<PreparedRTSActionRestore> prepare(
        const RTSActionCapture& captured, std::shared_ptr<ecs::Table> world,
        std::span<const action::ActionEntityBinding> bindings);

private:
    friend class ActionAdapter;
    struct Impl;
    explicit PreparedRTSActionRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
