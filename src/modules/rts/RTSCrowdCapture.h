#pragma once
/** @file RTSCrowdCapture.h
 * @brief rts module-interface: detached native Crowd state and complete Unit-to-agent relationships.
 * @remarks rts:crowd v1 owns backend/member/navigation persistence; no gameplay event replay or script binding.
 */
#include <memory>
#include <optional>
#include <span>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
namespace eve::crowd {
struct AgentFrame;
struct FlowRequestRef;
}  // namespace eve::crowd
namespace eve::rts {
class RTS;
class PreparedRTSCrowdRestore;
/** @brief Frozen Unit settings joined to a native frame row, including pending detach/rebind state.
 * @ownership Owns logical keys; frameIndex is native correspondence, never persistent agent identity.
 */
struct RTSCrowdMember {
    SubjectRef                 subject;
    SubjectRef                 container;
    std::string                key;
    std::string                bindingKey;
    float                      radius = .5f, heading = 0.f, x = 0.f, y = 0.f;
    std::optional<std::size_t> frameIndex;
};
/** @brief Detached canonical Crowd snapshot, frame and all owned Unit relationships.
 * @ownership No source Table, RTS, provider, live lease or component retained.
 * @thread Owner-thread capture outside Views/callbacks; detached access does not advance simulation.
 * @remarks Retains native requests and RTS shared-flow jobs/groups/quotas; map-provider tracking remains separate.
 */
class EVENGINE_API_DOMAINS RTSCrowdCapture final {
public:
    /** @brief Release frozen facts. */
    ~RTSCrowdCapture();
    /** @brief Transfer frozen facts. */
    RTSCrowdCapture(RTSCrowdCapture&&) noexcept;
    /** @brief Replace frozen facts. */
    RTSCrowdCapture& operator=(RTSCrowdCapture&&) noexcept;
    RTSCrowdCapture(const RTSCrowdCapture&)            = delete;
    RTSCrowdCapture& operator=(const RTSCrowdCapture&) = delete;
    /** @brief Return joint epoch, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow immutable rows until move/destruction; empty after move. */
    [[nodiscard]] std::span<const RTSCrowdMember> members() const noexcept;
    /** @brief Borrow canonical frame until move/destruction; empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const crowd::AgentFrame>> frame() const noexcept;

    /** @brief Seal complete backend, member correspondences and shared navigation jobs/groups/notices.
     * @param revision Nonzero joint save revision. @param tick Joint simulation boundary.
     * @param hash Injected synchronous digest provider, never retained.
     * @return Owning eve.rts-crowd / rts:crowd v1 envelope, without live scopes or ECS handles.
     * @ownership Copies all facts before callbacks; moved captures fail. @thread Quiescent owner thread.
     * @cost Copies full retained backend and navigation state per explicit save.
     * @throws std::bad_alloc Source and live worlds remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Admit complete CPU state, rebuilding historical backend references and typed relationships.
     * @param snapshot Owning versioned envelope. @param epoch Required nonnil joint save identity.
     * @param hash Injected synchronous verifier, never retained.
     * @return Detached facts or invalid version/fields, graph, boundary or correspondence failure.
     * @remarks Initial v1 has no migration; actual topology validation/publication still uses native prepare.
     * @thread CPU admission, no services, callbacks except hash, graph work or live publication.
     * @cost Validates and copies all retained data per explicit file restore.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<RTSCrowdCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                       const SnapshotHashProvider& hash);

private:
    friend class RTS;
    friend class PreparedRTSCrowdRestore;
    struct Impl;
    explicit RTSCrowdCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Unpublished native Crowd and body-binding candidate for one physical target scope.
 * @ownership Owns candidate state and keys; weakly observes actual target Table, RTS and provider.
 * @thread Owner thread outside Views/callbacks; source may die after preparation.
 */
class EVENGINE_API_DOMAINS PreparedRTSCrowdRestore final {
public:
    /** @brief Discard unpublished state. */
    ~PreparedRTSCrowdRestore();
    /** @brief Transfer candidate. */
    PreparedRTSCrowdRestore(PreparedRTSCrowdRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedRTSCrowdRestore& operator=(PreparedRTSCrowdRestore&&) noexcept;
    PreparedRTSCrowdRestore(const PreparedRTSCrowdRestore&)            = delete;
    PreparedRTSCrowdRestore& operator=(const PreparedRTSCrowdRestore&) = delete;
    /** @brief Prepare native state and references for complete already-rebuilt Unit topology.
     * @param target Idle owned RTS with live empty Crowd and pristine unbound Unit Crowd components.
     * @param captured Frozen source borrowed only during this call.
     * @return Candidate or missing component/provider, identity, pose, containment or topology failure.
     * @remarks Restores the native shared-flow service; does not create agents, publish live components or advance
     * time.
     * @cost Copies native Crowd storage and all body keys once per isolated restore attempt.
     * @throws std::bad_alloc No target state changed; the operation can retry.
     */
    [[nodiscard]] static Result<PreparedRTSCrowdRestore> prepare(RTS& target, const RTSCrowdCapture& captured);

    /** @brief Remap one saved native request into this unpublished body/backend candidate.
     * @return Fresh owner-qualified reference, or empty/foreign/released/stale source token failure.
     * @remarks Becomes usable only after successful RTS::restoreCrowd; cancellation publishes nothing.
     * @ownership Value only, no source world or VM retained. @thread Owner thread, no callbacks.
     */
    [[nodiscard]] Result<crowd::FlowRequestRef> remapFlowRequest(crowd::FlowRequestRef source) const;

private:
    friend class RTS;
    struct Impl;
    explicit PreparedRTSCrowdRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
