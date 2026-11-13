#pragma once
/** @file RTSAttributeCapture.h
 * @brief rts module-interface: complete native Unit attribute scope and atomic exact-topology restoration.
 * @remarks Uses canonical projection values and rts:unit-attributes v1; no new systems, events, scripts or tools.
 */
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
namespace eve::attributes {
class AttributeProjectionCapture;
}
namespace eve::rts {
class RTS;
/** @brief One stable Unit identity and independent immutable full projection facts.
 * @ownership Shares detached native storage, never an entity, Table, provider or runtime handle.
 */
struct RTSUnitAttributeMember {
    SubjectRef                                                    subject;
    std::shared_ptr<const attributes::AttributeProjectionCapture> attributes;
};
/** @brief Complete Unit attribute scope, including values outside the selective combat adapter.
 * @ownership Owns exact projection revisions, initialization flags and native modifier/id counters.
 * @thread Owner-thread capture outside Views/callbacks; detached access does not evaluate custom policies.
 */
class EVENGINE_API_DOMAINS RTSUnitAttributeCapture final {
public:
    /** @brief Release detached facts. */
    ~RTSUnitAttributeCapture();
    /** @brief Transfer facts. */
    RTSUnitAttributeCapture(RTSUnitAttributeCapture&&) noexcept;
    /** @brief Replace facts. */
    RTSUnitAttributeCapture& operator=(RTSUnitAttributeCapture&&) noexcept;
    RTSUnitAttributeCapture(const RTSUnitAttributeCapture&)            = delete;
    RTSUnitAttributeCapture& operator=(const RTSUnitAttributeCapture&) = delete;
    /** @brief Joint checkpoint scope, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow subject-ordered rows until move/destruction; empty after move. */
    [[nodiscard]] std::span<const RTSUnitAttributeMember> members() const noexcept;
    /** @brief Seal the complete subject-ordered Unit scope through canonical projection codecs.
     * @param revision Nonzero joint save revision, independent of each projection's own revision.
     * @param tick Injected joint boundary; game coordinator joins phase-specific simulation clocks.
     * @param hash Explicit digest borrowed only during this call.
     * @return rts:unit-attributes v1 envelope or moved/revision/projection/digest diagnostics.
     * @ownership All payload facts are copied before digest callbacks; no source world or entity is consulted.
     * @cost Encodes and hashes all Unit projections once per explicit persistent sealing.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Verify and admit complete ordered members without resolving or creating entities.
     * @param snapshot Owned input copied before digest callbacks.
     * @param epoch Expected joint save identity; coordinator separately joins revision/tick and roots.
     * @param hash Explicit digest borrowed during verification only.
     * @return Independent native capture or integrity/version/field/identity/projection diagnostics.
     * @remarks Unknown fields/versions, duplicate/noncanonical/out-of-order identities reject. No implicit
     * migration. Existing PreparedRTSUnitAttributeRestore still checks exact live topology and physical owners;
     * custom policy implementation revisions remain separately admitted by domain composition.
     * @cost Parses and copies all Unit projections once per explicit admission; no capability registration.
     */
    [[nodiscard]] static Result<RTSUnitAttributeCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                               const SnapshotHashProvider& hash);


private:
    friend class RTS;
    struct Impl;
    explicit RTSUnitAttributeCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Unpublished native projection candidates bound to one physical RTS and exact target Unit topology.
 * @ownership Weakly observes the target world/facade; source facts and prepared values are independently owned.
 * @thread Owner thread outside Views/callbacks. Never creates components, evaluates policies or registers services.
 */
class EVENGINE_API_DOMAINS PreparedRTSUnitAttributeRestore final {
public:
    /** @brief Discard unpublished values. */
    ~PreparedRTSUnitAttributeRestore();
    /** @brief Transfer candidates. */
    PreparedRTSUnitAttributeRestore(PreparedRTSUnitAttributeRestore&&) noexcept;
    /** @brief Replace candidates. */
    PreparedRTSUnitAttributeRestore& operator=(PreparedRTSUnitAttributeRestore&&) noexcept;
    PreparedRTSUnitAttributeRestore(const PreparedRTSUnitAttributeRestore&)            = delete;
    PreparedRTSUnitAttributeRestore& operator=(const PreparedRTSUnitAttributeRestore&) = delete;
    /** @brief Prepare every saved projection against the complete existing target Unit scope.
     * @return Candidate or moved capture, borrowed/busy world, topology, identity, subject or owner conflict.
     * @ownership Capture is borrowed only during this call; target roots/components must already exist.
     * @remarks Source and target Unit subjects must match exactly; source ECS generations are not copied.
     * Further target topology changes invalidate publication. Other root fields, clocks and policies are external.
     * @cost Copies every native projection and indexes target identities once per restoration attempt.
     * @throws std::bad_alloc No source or target state was changed; preparation can retry.
     */
    [[nodiscard]] static Result<PreparedRTSUnitAttributeRestore> prepare(RTS&                           target,
                                                                         const RTSUnitAttributeCapture& captured);

private:
    friend class RTS;
    struct Impl;
    explicit PreparedRTSUnitAttributeRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
