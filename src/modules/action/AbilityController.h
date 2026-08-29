#pragma once

/** @file AbilityController.h @brief Shared player and AI ability-intent routing. */

#include "action/AbilitySystem.h"

#include <cstddef>
#include <deque>
#include <optional>

namespace eve::action {

/** @brief Owning request to activate one granted ability. */
struct AbilityIntent {
    AbilityGrantId grantId;
    ActionRequest  request;
};

/**
 * @brief Pull boundary shared by player input queues and AI decision adapters.
 *
 * Implementations return an owning intent and must not retain the borrowed
 * tick. The caller invokes this boundary without holding an engine lock.
 */
class IAbilityIntentSource {
public:
    virtual ~IAbilityIntentSource() = default;

    /** @brief Produce at most one intent for the supplied deterministic simulation tick. */
    [[nodiscard]] virtual Result<std::optional<AbilityIntent>> nextIntent(SimulationTick tick) = 0;
};

/** @brief Owner-thread FIFO adapter for player input and command bindings. */
class PlayerAbilityIntentQueue final : public IAbilityIntentSource {
public:
    /** @brief Append an owning player intent; zero grant ids are rejected. */
    [[nodiscard]] Result<void> enqueue(AbilityIntent intent);
    /** @brief Consume the oldest queued player intent, if any. */
    [[nodiscard]] Result<std::optional<AbilityIntent>> nextIntent(SimulationTick tick) override;
    /** @brief Number of pending player intents. */
    [[nodiscard]] std::size_t pendingCount() const noexcept { return intents_.size(); }
    /** @brief Discard all pending player intents. */
    void clear() noexcept { intents_.clear(); }

private:
    std::deque<AbilityIntent> intents_;
};

/**
 * @brief Shared controller that routes player or AI intents into AbilityRuntime.
 *
 * The borrowed ability runtime must outlive this controller. Each call consumes
 * at most one intent. Source failure performs no ability mutation; once a source
 * returns an intent it is consumed exactly once, including rejected activations.
 */
class AbilityControllerRuntime {
public:
    /** @brief Construct a controller borrowing the canonical ability runtime. */
    explicit AbilityControllerRuntime(AbilityRuntime& abilities) : abilities_(abilities) {}

    /**
     * @brief Pull and activate at most one intent using the supplied deterministic tick.
     * @return Empty success when the source has no intent, or the owning activation record.
     */
    [[nodiscard]] Result<std::optional<AbilityActivation>> processNext(IAbilityIntentSource& source,
                                                                        SimulationTick         tick);

private:
    AbilityRuntime& abilities_;
};

}  // namespace eve::action
