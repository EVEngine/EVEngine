#pragma once

/**
 * @file CardEffects.h
 * @brief Card-domain effect adapter and executor.
 *
 * The adapter owns no second effect store: `effects::EffectContainer` is the
 * sole owner of live instances. Card code only supplies typed policy and
 * interprets periodic triggers (shield/damage/heal).
 */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::card {

/** @brief Card-specific interpretation of an effect magnitude. */
enum class CardEffectKind : std::uint8_t { Damage, Heal, Shield };

/** @brief Mutable card combat state changed by the card effect executor. */
struct CardEffectTarget {
    int           health        = 100;
    int           maxHealth     = 100;
    int           barrier       = 0;
    std::uint32_t deathTriggers = 0;
};

/** @brief Strong card definition projected into the common effect definition. */
struct CardEffectDefinition {
    std::string              id;
    std::string              source;
    double                   duration  = 0.0;
    double                   period    = 0.0;
    double                   magnitude = 0.0;
    effects::EffectPolicy    policy;
    CardEffectKind           kind         = CardEffectKind::Damage;
    bool                     deathTrigger = true;
    std::vector<std::string> tags;
};

/** @brief Result of one card effect lifecycle step and its domain settlement. */
struct CardEffectUpdate {
    effects::EffectUpdateSummary lifecycle;
    std::uint32_t                settled        = 0;
    std::uint32_t                absorbed       = 0;
    bool                         deathTriggered = false;
};

/** @brief Serializable in-memory card effect snapshot. */
struct CardEffectSnapshot {
    effects::EffectContainer effects;
    CardEffectTarget         target;
};

/** @brief Card executor: interprets card policy without owning effect instances. */
class CardEffectExecutor {
public:
    /** @brief Apply immediate shield semantics to a staged target. */
    [[nodiscard]] eve::Result<void> applyImmediate(CardEffectTarget&              target,
                                                   const effects::EffectInstance& effect) const;

    /** @brief Settle all common periodic triggers using card shield/death rules. */
    [[nodiscard]] eve::Result<CardEffectUpdate> settle(CardEffectTarget&            target,
                                                       effects::EffectUpdateSummary lifecycle) const;
};

/** @brief Card adapter composing the common lifecycle container with CardEffectExecutor. */
class CardEffectAdapter {
public:
    /**
     * @brief Seed the domain target before the first effect is applied.
     * @param target Initial current/max health and barrier state owned by the card.
     * @return Applied, or a conflict when active effects already exist.
     */
    [[nodiscard]] eve::Result<void> initializeTarget(CardEffectTarget target);
    /** @brief Apply one typed card effect and return its generation-qualified handle. */
    [[nodiscard]] eve::Result<effects::EffectHandle> apply(const CardEffectDefinition& definition,
                                                           eve::SubjectRef             subject);
    /** @brief Remove one effect handle; stale handles are rejected. */
    [[nodiscard]] eve::Result<void> remove(effects::EffectHandle handle);
    /** @brief Advance and settle card effects atomically for one simulation step. */
    [[nodiscard]] eve::Result<CardEffectUpdate> advance(const eve::SimulationStep& step);
    /** @brief Return the number of active card effects. */
    [[nodiscard]] std::size_t count() const noexcept;
    /** @brief Capture a deep lifecycle snapshot. */
    [[nodiscard]] CardEffectSnapshot snapshot() const;
    /** @brief Restore a snapshot and invalidate every pre-restore effect handle. */
    [[nodiscard]] eve::Result<void> restore(const CardEffectSnapshot& snapshot);
    /** @brief Resolve a live handle as a borrowed observation. */
    [[nodiscard]] eve::Result<const effects::EffectInstance*> resolve(effects::EffectHandle handle) const;
    /** @brief Return the mutable card target state owned by this adapter. */
    [[nodiscard]] CardEffectTarget& target() noexcept { return target_; }
    /** @brief Return the read-only card target state. */
    [[nodiscard]] const CardEffectTarget& target() const noexcept { return target_; }

private:
    effects::EffectContainer container_;
    CardEffectExecutor       executor_;
    CardEffectTarget         target_;
};

}  // namespace eve::card
