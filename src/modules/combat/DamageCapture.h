#pragma once
/** @file DamageCapture.h
 * @brief combat module-interface: detached native damage policy and declarative settlement configuration.
 * @remarks Native facts and combat:damage-runtime v1 persistence; no target state, events or external provider
 * retained.
 */
#include <memory>
#include "common/Export.h"
#include "common/Snapshot.h"
namespace eve::combat {
class DamageRuntime;
/** @brief Own reaction-policy values and canonical rule text, independent of source services and compiled callbacks.
 * @ownership Pure CPU data; source runtime and rule set may be destroyed. No clock or RNG is consulted.
 * @thread Capture/materialization on simulation owner thread outside callbacks; no callbacks on destruction.
 */
class EVENGINE_API_BACKENDS DamageCapture final {
public:
    /** @brief Release native facts. */
    ~DamageCapture();
    /** @brief Transfer native facts. */
    DamageCapture(DamageCapture&&) noexcept;
    /** @brief Replace native facts by move. */
    DamageCapture& operator=(DamageCapture&&) noexcept;
    DamageCapture(const DamageCapture&)            = delete;
    DamageCapture& operator=(const DamageCapture&) = delete;
    /** @brief Seal reaction policy and canonical settlement.rules using combat:damage-runtime v1.
     * @param epoch Nonnil joint save identity.
     * @param revision Nonzero joint revision.
     * @param tick Injected joint simulation boundary.
     * @param hash Explicit digest borrowed during this call only; not retained.
     * @return Owning envelope or moved/identity/digest failure, without modifying facts.
     * @remarks Payload rejects unknown fields; unsupported versions require an explicit future migration.
     * @thread Owner thread outside callbacks; no live services are consulted.
     * @cost Copies and hashes rule-sized payload per explicit persistent sealing.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Verify and admit a detached v1 envelope through canonical rule/configuration validation.
     * @param snapshot Owning input copied before digest callbacks; no caller storage is retained.
     * @param epoch Expected nonnil joint save identity; caller also joins revision/tick with other participants.
     * @param hash Explicit digest borrowed during verification only.
     * @return Independent native capture or integrity/schema/field/configuration diagnostics.
     * @remarks Unknown fields and versions reject; no implicit migration or external implementation fallback.
     * @thread Content/simulation owner thread; no live world or provider callbacks.
     * @cost Parses and compiles rule-sized facts per explicit admission; no observable publication.
     */
    [[nodiscard]] static Result<DamageCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                     const SnapshotHashProvider& hash);

private:
    friend class DamageRuntime;
    struct Data;
    explicit DamageCapture(std::unique_ptr<Data> data);
    std::unique_ptr<Data> data_;
};
}  // namespace eve::combat
