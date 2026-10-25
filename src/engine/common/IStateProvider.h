#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/StateValue.h"

#include <string>

namespace eve::caps {

/** @brief Native-state behavior during a live script reload. */
enum class StateReloadPolicy {
    Preserve, /**< Capture and restore the provider's live state. */
    Reset,    /**< Do not capture; rebuild the provider from new definitions. */
};

/**
 * @brief Runtime-state serialization for state hot reload.
 *
 * A module that owns mutable runtime state (animation state machines, dialogue
 * sessions, RPG casting, card phases) implements this and registers with
 * eve::cap::addListener<IStateProvider>(). A reload session captures every
 * provider before code/asset reload and restores the captured state afterwards,
 * so running state survives definition changes instead of being reset.
 *
 * Modules that do not register a provider are treated as stateless and are
 * simply skipped — the same trimming story as IAssetReloader.
 */
class EVENGINE_API_FOUNDATION_INLINE IStateProvider {
public:
    static constexpr const char* capabilityName = "IStateProvider";
    /** @brief I state provider. */
    virtual ~IStateProvider()                   = default;

    /** @brief Stable kind label, e.g. "dialogue" / "anim" / "rpg.casting". */
    virtual const char* stateKind() const = 0;

    /**
     * @brief Capture an owning, canonical snapshot of this provider.
     * @return Snapshot or a structured failure; no state is changed on failure.
     * @thread Owner thread during a paused reload transaction.
     * @reentrancy Does not invoke script callbacks.
     */
    [[nodiscard]] virtual Result<Value> capture() = 0;

    /**
     * @brief Restore captured state onto the (possibly new) definitions.
     * @return Structured failure when the state is incompatible with the current
     *         definitions. Snapshot restore rolls affected providers back on failure.
     * @note Must accept this provider's own capture() result against unchanged
     *       definitions, including after a failed restore/reset, so rollback can
     *       recover its pre-operation state. Must not invoke script callbacks.
     */
    [[nodiscard]] virtual Result<void> restore(const Value& in) = 0;

    /** @brief Reset this provider to its default state (Reset reload policy). */
    [[nodiscard]] virtual Result<void> resetToDefaults() = 0;

    /** @brief Whether live state is preserved or reset across code reload. */
    virtual StateReloadPolicy reloadPolicy() const { return StateReloadPolicy::Preserve; }
};

}  // namespace eve::caps
