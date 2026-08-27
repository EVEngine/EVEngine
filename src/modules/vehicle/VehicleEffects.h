#pragma once

/**
 * @file VehicleEffects.h
 * @brief Vehicle-domain effect adapter and armor-zone executor.
 */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::vehicle {

/** @brief Vehicle-specific effect strategy. */
enum class VehicleEffectKind : std::uint8_t { Damage, Repair, Disable };

/** @brief Mutable vehicle state changed by the effect executor. */
struct VehicleEffectTarget {
    double hull = 100.0;
    double maxHull = 100.0;
    bool disabled = false;
    std::uint32_t criticalHits = 0;
    double frontArmorMultiplier = 0.75;
    double sideArmorMultiplier = 1.0;
    double rearArmorMultiplier = 1.25;
};

/** @brief Strong vehicle definition projected into the common lifecycle. */
struct VehicleEffectDefinition {
    std::string id;
    std::string source;
    std::string armorZone = "side";
    double duration = 0.0;
    double period = 0.0;
    double magnitude = 0.0;
    effects::EffectPolicy policy;
    VehicleEffectKind kind = VehicleEffectKind::Damage;
    std::vector<std::string> tags;
};

/** @brief Result of one vehicle lifecycle step and armor settlement. */
struct VehicleEffectUpdate {
    effects::EffectUpdateSummary lifecycle;
    std::uint32_t settled = 0;
    std::uint32_t criticalHits = 0;
};

/** @brief In-memory vehicle effect snapshot. */
struct VehicleEffectSnapshot {
    effects::EffectContainer effects;
    VehicleEffectTarget target;
};

/** @brief Vehicle executor applying armor-zone and disable strategies. */
class VehicleEffectExecutor {
public:
    /** @brief Validate hull and armor invariants. */
    [[nodiscard]] eve::Result<void> validate(const VehicleEffectTarget& target) const;
    /** @brief Apply an immediate disable strategy to a staged target. */
    [[nodiscard]] eve::Result<void> applyImmediate(VehicleEffectTarget& target,
                                                   const effects::EffectInstance& effect) const;
    /** @brief Settle common periodic triggers through vehicle armor policy. */
    [[nodiscard]] eve::Result<VehicleEffectUpdate> settle(VehicleEffectTarget& target,
                                                          effects::EffectUpdateSummary lifecycle) const;
};

/** @brief Vehicle adapter with one common container and one typed executor. */
class VehicleEffectAdapter {
public:
    /** @brief Apply one typed vehicle effect to a stable subject. */
    [[nodiscard]] eve::Result<effects::EffectHandle> apply(const VehicleEffectDefinition& definition,
                                                            eve::SubjectRef subject);
    /** @brief Remove one generation-qualified effect handle. */
    [[nodiscard]] eve::Result<void> remove(effects::EffectHandle handle);
    /** @brief Advance and settle vehicle effects atomically. */
    [[nodiscard]] eve::Result<VehicleEffectUpdate> advance(const eve::SimulationStep& step);
    /** @brief Capture effects and target state. */
    [[nodiscard]] VehicleEffectSnapshot snapshot() const;
    /** @brief Restore a snapshot and invalidate prior handles. */
    [[nodiscard]] eve::Result<void> restore(const VehicleEffectSnapshot& snapshot);
    /** @brief Resolve a live handle as a borrowed observation. */
    [[nodiscard]] eve::Result<const effects::EffectInstance*> resolve(effects::EffectHandle handle) const;
    /** @brief Access the mutable target state. */
    [[nodiscard]] VehicleEffectTarget& target() noexcept { return target_; }
    /** @brief Read the target state. */
    [[nodiscard]] const VehicleEffectTarget& target() const noexcept { return target_; }

private:
    effects::EffectContainer container_;
    VehicleEffectExecutor executor_;
    VehicleEffectTarget target_;
};

}  // namespace eve::vehicle
