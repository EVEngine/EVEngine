#pragma once

/**
 * @file WeaponEffects.h
 * @brief Weapon-domain effect adapter and heat/jam executor.
 */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::weapon {

/** @brief Weapon-specific effect strategy. */
enum class WeaponEffectKind : std::uint8_t { Heat, Jam, Recoil };

/** @brief Mutable weapon state interpreted by the typed executor. */
struct WeaponEffectTarget {
    double        heat         = 0.0;
    double        maxHeat      = 100.0;
    bool          jammed       = false;
    double        recoil       = 0.0;
    std::uint32_t blockedShots = 0;
};

/** @brief Typed weapon definition projected into the common lifecycle. */
struct WeaponEffectDefinition {
    std::string              id;
    std::string              source;
    double                   duration  = 0.0;
    double                   period    = 0.0;
    double                   magnitude = 0.0;
    effects::EffectPolicy    policy;
    WeaponEffectKind         kind = WeaponEffectKind::Heat;
    std::vector<std::string> tags;
};

/** @brief Result of one weapon lifecycle step and domain settlement. */
struct WeaponEffectUpdate {
    effects::EffectUpdateSummary lifecycle;
    std::uint32_t                settled = 0;
    bool                         jammed  = false;
};

/** @brief In-memory weapon effect snapshot. */
struct WeaponEffectSnapshot {
    effects::EffectContainer effects;
    WeaponEffectTarget       target;
};

/** @brief Weapon executor applying heat, jam and recoil rules. */
class WeaponEffectExecutor {
public:
    /** @brief Validate heat and recoil invariants. */
    [[nodiscard]] eve::Result<void> validate(const WeaponEffectTarget& target) const;
    /** @brief Apply an immediate jam/recoil strategy to a staged target. */
    [[nodiscard]] eve::Result<void> applyImmediate(WeaponEffectTarget&            target,
                                                   const effects::EffectInstance& effect) const;
    /** @brief Settle common periodic triggers through weapon policy. */
    [[nodiscard]] eve::Result<WeaponEffectUpdate> settle(WeaponEffectTarget&          target,
                                                         effects::EffectUpdateSummary lifecycle) const;
};

/** @brief Weapon adapter with a single common lifecycle owner. */
class WeaponEffectAdapter {
public:
    /** @brief Apply one typed weapon effect to a stable subject. */
    [[nodiscard]] eve::Result<effects::EffectHandle> apply(const WeaponEffectDefinition& definition,
                                                           eve::SubjectRef               subject);
    /** @brief Remove one generation-qualified effect handle. */
    [[nodiscard]] eve::Result<void> remove(effects::EffectHandle handle);
    /** @brief Advance and settle weapon effects atomically. */
    [[nodiscard]] eve::Result<WeaponEffectUpdate> advance(const eve::SimulationStep& step);
    /** @brief Capture effects and target state. */
    [[nodiscard]] WeaponEffectSnapshot snapshot() const;
    /** @brief Restore a snapshot and invalidate prior handles. */
    [[nodiscard]] eve::Result<void> restore(const WeaponEffectSnapshot& snapshot);
    /** @brief Resolve a live handle as a borrowed observation. */
    [[nodiscard]] eve::Result<const effects::EffectInstance*> resolve(effects::EffectHandle handle) const;
    /** @brief Access the mutable weapon target state. */
    [[nodiscard]] WeaponEffectTarget& target() noexcept { return target_; }
    /** @brief Read the weapon target state. */
    [[nodiscard]] const WeaponEffectTarget& target() const noexcept { return target_; }

private:
    effects::EffectContainer container_;
    WeaponEffectExecutor     executor_;
    WeaponEffectTarget       target_;
};

}  // namespace eve::weapon
