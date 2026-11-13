#pragma once
/** @file SensingCapture.h
 * @brief sensing module-interface: immutable facts/index policy and prepared native restoration.
 * @remarks sensing:world-runtime v1 preserves facts and index policy; no live providers or ECS links retained.
 */
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Snapshot.h"
namespace eve::sensing {
class SensingWorld;
struct Subject;
enum class SpatialIndexKind : std::uint8_t;
/** @brief Immutable native facts and spatial policy; query outputs/debug and derived index ids are excluded.
 * @ownership Owns strings/sets/coordinates. Source retirement cannot invalidate facts; no callbacks retained.
 * @thread Capture on world owner thread outside provider callbacks; detached reads invoke no services.
 */
class EVENGINE_API_PLATFORM SensingCapture final {
public:
    /** @brief Release facts. */
    ~SensingCapture();
    /** @brief Transfer facts. */
    SensingCapture(SensingCapture&&) noexcept;
    /** @brief Replace facts. */
    SensingCapture& operator=(SensingCapture&&) noexcept;
    SensingCapture(const SensingCapture&)            = delete;
    SensingCapture& operator=(const SensingCapture&) = delete;
    /** @brief Borrow id-ordered facts until move/destruction; empty after move. */
    [[nodiscard]] std::span<const Subject> subjects() const noexcept;
    /** @brief Exact source index kind, or empty after move. */
    [[nodiscard]] std::optional<SpatialIndexKind> indexKind() const noexcept;
    /** @brief Source cell size; 1 for None, 0 after move. */
    [[nodiscard]] float cellSize() const noexcept;
    /** @brief Seal frozen facts and index policy in sensing:world-runtime v1.
     * @param epoch Nonnil joint save identity.
     * @param revision Nonzero joint revision.
     * @param tick Injected joint simulation boundary.
     * @param hash Explicit digest borrowed only during this call.
     * @return Owning envelope or moved/identity/digest diagnostics, without consulting a live world.
     * @remarks Outer and index fields are strict; nested world fields use the canonical legacy codec policy.
     * @cost Copies and hashes subject-sized data per explicit persistent sealing; no derived index build.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Verify and admit frozen facts through canonical world parsing and index rebuilding.
     * @param snapshot Owning input copied before digest callbacks, retained only during admission.
     * @param epoch Expected joint save identity; coordinator separately joins revision/tick.
     * @param hash Explicit digest borrowed only during verification.
     * @return Independent capture or schema/integrity/field/index diagnostics; no live destination is mutated.
     * @remarks Unknown outer versions/fields reject. Nested world v1/v2 migration and unknown-field handling
     * follow SensingWorld::restoreJson. No provider, source world or callback is retained.
     * @thread Content/simulation owner thread, outside world operations and callbacks.
     * @cost Parses subject-sized facts and rebuilds the derived index per explicit admission.
     */
    [[nodiscard]] static Result<SensingCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                      const SnapshotHashProvider& hash);


private:
    friend class SensingWorld;
    friend class PreparedSensingRestore;
    struct Impl;
    explicit SensingCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Fully built independent native world candidate, with no source or destination lifetime dependency. */
class EVENGINE_API_PLATFORM PreparedSensingRestore final {
public:
    /** @brief Release unpublished state. */
    ~PreparedSensingRestore();
    /** @brief Transfer candidate. */
    PreparedSensingRestore(PreparedSensingRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedSensingRestore& operator=(PreparedSensingRestore&&) noexcept;
    PreparedSensingRestore(const PreparedSensingRestore&)            = delete;
    PreparedSensingRestore& operator=(const PreparedSensingRestore&) = delete;
    /** @brief Copy detached facts and build their exact index using the canonical native rebuild path.
     * @return Candidate or moved/invalid input, without mutating source or any destination.
     * @thread Owner thread outside provider callbacks; no LOS/query/service calls.
     * @cost Copies subject storage and builds the derived spatial index per restore attempt.
     * @throws std::bad_alloc Candidate allocation failed without observable publication.
     */
    [[nodiscard]] static Result<PreparedSensingRestore> prepare(const SensingCapture& captured);

private:
    friend class SensingWorld;
    explicit PreparedSensingRestore(std::unique_ptr<SensingWorld> world);
    std::unique_ptr<SensingWorld> world_;
};
}  // namespace eve::sensing
