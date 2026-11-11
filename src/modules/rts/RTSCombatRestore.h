#pragma once
/** @file RTSCombatRestore.h
 * @brief rts module-interface: native sensing facts and combat ownership/latches restored together.
 * @remarks No persistent schema, ECS mutations, events, scripts or tool bindings.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::sensing {
class SensingCapture;
}
namespace eve::rts {
class RTS;
struct RTSCombatSnapshot;
/** @brief Independent candidate weakly bound to a physical RTS, its owned Table and sensing provider.
 * @ownership Owns copied facts and prepared native index; retains no source or target owner.
 * @thread Simulation owner thread, outside Views and callbacks; no services execute.
 */
class EVENGINE_API_DOMAINS PreparedRTSCombatRestore final {
public:
    /** @brief Discard unpublished facts. */
    ~PreparedRTSCombatRestore();
    /** @brief Transfer candidate. */
    PreparedRTSCombatRestore(PreparedRTSCombatRestore&&) noexcept;
    /** @brief Replace candidate. */
    PreparedRTSCombatRestore& operator=(PreparedRTSCombatRestore&&) noexcept;
    PreparedRTSCombatRestore(const PreparedRTSCombatRestore&)            = delete;
    PreparedRTSCombatRestore& operator=(const PreparedRTSCombatRestore&) = delete;
    /** @brief Prepare complete sensing storage and bind mirror ownership to actual owned Unit/Building incarnations.
     * @return Candidate or absent provider, borrowed Table, invalid facts or missing/ambiguous body identity.
     * @remarks Replaces the entire sensing scope, including non-RTS facts. Coordinator must own this scope and
     * separately join roots, clocks, event queues and provider implementations at the same checkpoint boundary.
     * @cost Copies subjects/sets and builds their native spatial index once per restore attempt.
     * @throws std::bad_alloc Preparation failed without changing source or target.
     */
    [[nodiscard]] static Result<PreparedRTSCombatRestore> prepare(RTS& target, const RTSCombatSnapshot& combat,
                                                                  const sensing::SensingCapture& sensing);

private:
    friend class RTS;
    struct Impl;
    explicit PreparedRTSCombatRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
