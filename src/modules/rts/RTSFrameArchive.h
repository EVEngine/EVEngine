#pragma once
/** @file RTSFrameArchive.h
 * @brief rts module-interface: detached versioned frame queues and publication cursors, without event replay.
 * @remarks Persistence data only; roots, action ownership and consumer acknowledgements join at the game boundary.
 */
#include <memory>
#include <span>
#include <vector>
#include "common/BorrowedRef.h"
#include "common/Snapshot.h"
namespace eve::rts {
struct RTSFrameState;
struct RTSFrameDamageEvent;
/** @brief Immutable frame participant owning complete damage audits, all queues and their exact watermarks.
 * @ownership No source world, service, callback or runtime lease retained. Historical subjects may be retired.
 * @thread Capture on simulation owner outside Views; detached admission/sealing invokes only the explicit digest.
 */
class EVENGINE_API_DOMAINS RTSFrameArchive final {
public:
    /** @brief Release detached facts. */
    ~RTSFrameArchive();
    /** @brief Transfer detached facts. */
    RTSFrameArchive(RTSFrameArchive&&) noexcept;
    /** @brief Replace detached facts. */
    RTSFrameArchive& operator=(RTSFrameArchive&&) noexcept;
    RTSFrameArchive(const RTSFrameArchive&)            = delete;
    RTSFrameArchive& operator=(const RTSFrameArchive&) = delete;
    /** @brief Encode retained damage observations using the canonical frame event/audit codec.
     * @return Owning ordered array, including duplicates, or invalid event/audit diagnostics; at most 1M rows.
     * @remarks No queue-order/watermark admission, deduplication, event replay or live state mutation.
     * @ownership Borrows inputs only during call. @thread CPU save, no callbacks or services.
     * @cost Copies and validates complete event/audit payloads per explicit handoff save.
     */
    [[nodiscard]] static Result<Value> encodeDamageInputs(std::span<const RTSFrameDamageEvent> inputs);
    /** @brief Admit retained observations without applying complete-frame queue ordering rules.
     * @return Owning ordered rows, including duplicates, or strict field/audit/size diagnostics.
     * @ownership Borrows value only during call. @thread CPU admission, no callbacks or services.
     * @cost Parses and copies complete event/audit payloads per explicit handoff load.
     */
    [[nodiscard]] static Result<std::vector<RTSFrameDamageEvent>> prepareDamageInputs(const Value& value);
    /** @brief Validate and freeze a complete actual frame using the canonical field and queue admission kernels.
     * @param state Owning copied facts; no source world is retained or queried.
     * @param epoch Nonnil joint save identity.
     * @param revision Nonzero joint revision.
     * @param tick Joint game boundary; retained events may precede it.
     * @return Independent participant or invalid field/audit/sequence diagnostics; no world is changed.
     * @cost Copies and validates frame-sized queues/audits per explicit capture.
     */
    [[nodiscard]] static Result<RTSFrameArchive> capture(RTSFrameState state, PersistentId epoch, Revision revision,
                                                         SimulationTick tick);
    /** @brief Seal rts:frame-state v1, including semantic audits and separate event transport metadata.
     * @param hash Explicit digest borrowed during this call only.
     * @return Owning envelope or moved/digest diagnostics. Facts are pinned before callbacks.
     * @remarks Strict fields and tuple sizes; unknown versions require explicit future migration.
     * @cost Encodes and hashes frame-sized payload per explicit persistent sealing.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(const SnapshotHashProvider& hash) const;
    /** @brief Verify an owning envelope and admit all fields/queues without resolving or replaying events.
     * @param snapshot Input copied before digest callbacks; caller storage is not retained.
     * @param epoch Expected joint save identity; coordinator separately checks revision/tick.
     * @param hash Explicit digest borrowed during verification only.
     * @return Independent participant or integrity/schema/field/audit/queue diagnostics, without publication.
     * @cost Parses and validates frame-sized payload per explicit admission.
     */
    [[nodiscard]] static Result<RTSFrameArchive> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                       const SnapshotHashProvider& hash);
    /** @brief Borrow immutable facts until archive move/destruction; empty after move. */
    [[nodiscard]] OptionalRef<const RTSFrameState> state() const noexcept;
    /** @brief Joint identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Joint revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Joint boundary, or zero after move. */
    [[nodiscard]] SimulationTick tick() const noexcept;

private:
    struct Data;
    explicit RTSFrameArchive(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::rts
