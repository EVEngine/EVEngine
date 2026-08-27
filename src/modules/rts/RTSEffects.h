#pragma once

/**
 * @file RTSEffects.h
 * @brief RTS effect adapter with morale, suppression and production policies.
 */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "effects/EffectContainer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::rts {

/** @brief RTS-specific interpretation of an effect instance. */
enum class RTSEffectKind : std::uint8_t { Morale, Suppression, ProductionLock };

/** @brief Mutable RTS state affected by the domain executor. */
struct RTSEffectTarget {
    double morale = 100.0;
    std::uint32_t suppression = 0;
    bool productionLocked = false;
    std::uint32_t commandInterrupts = 0;
};

/** @brief Typed RTS definition projected to the common lifecycle schema. */
struct RTSEffectDefinition {
    std::string id;
    std::string source;
    double duration = 0.0;
    double period = 0.0;
    double magnitude = 0.0;
    effects::EffectPolicy policy;
    RTSEffectKind kind = RTSEffectKind::Morale;
    std::vector<std::string> tags;
};

/** @brief Result of one RTS lifecycle step and domain settlement. */
struct RTSEffectUpdate {
    effects::EffectUpdateSummary lifecycle;
    std::uint32_t settled = 0;
    std::uint32_t commandInterrupts = 0;
};

/** @brief Serializable in-memory RTS effect snapshot. */
struct RTSEffectSnapshot {
    effects::EffectContainer effects;
    RTSEffectTarget target;
};

/** @brief RTS executor for morale, suppression and production-lock semantics. */
class RTSEffectExecutor {
public:
    /** @brief Validate the target state before a staged application. */
    [[nodiscard]] eve::Result<void> validate(const RTSEffectTarget& target) const;
    /** @brief Apply immediate production-lock semantics to a staged target. */
    [[nodiscard]] eve::Result<void> applyImmediate(RTSEffectTarget& target,
                                                   const effects::EffectInstance& effect) const;
    /** @brief Settle common periodic triggers using RTS-specific policies. */
    [[nodiscard]] eve::Result<RTSEffectUpdate> settle(RTSEffectTarget& target,
                                                      effects::EffectUpdateSummary lifecycle) const;
};

/** @brief RTS adapter that keeps the common container as the sole instance owner. */
class RTSEffectAdapter {
public:
    /** @brief Apply one typed effect to a stable subject. */
    [[nodiscard]] eve::Result<effects::EffectHandle> apply(const RTSEffectDefinition& definition,
                                                            eve::SubjectRef subject);
    /** @brief Remove one generation-qualified effect handle. */
    [[nodiscard]] eve::Result<void> remove(effects::EffectHandle handle);
    /** @brief Advance and settle RTS effects atomically. */
    [[nodiscard]] eve::Result<RTSEffectUpdate> advance(const eve::SimulationStep& step);
    /** @brief Return the number of active effect instances. */
    [[nodiscard]] std::size_t count() const noexcept;
    /** @brief Capture effects and domain target state. */
    [[nodiscard]] RTSEffectSnapshot snapshot() const;
    /** @brief Restore effects and target state, invalidating prior handles. */
    [[nodiscard]] eve::Result<void> restore(const RTSEffectSnapshot& snapshot);
    /** @brief Resolve a live handle as a borrowed observation. */
    [[nodiscard]] eve::Result<const effects::EffectInstance*> resolve(effects::EffectHandle handle) const;
    /** @brief Access the adapter-owned RTS target state. */
    [[nodiscard]] RTSEffectTarget& target() noexcept { return target_; }
    /** @brief Read the adapter-owned RTS target state. */
    [[nodiscard]] const RTSEffectTarget& target() const noexcept { return target_; }

private:
    effects::EffectContainer container_;
    RTSEffectExecutor executor_;
    RTSEffectTarget target_;
};

/**
 * @brief Entity-local RTS effect component backed by the canonical adapter.
 *
 * The component owns no second effect store. Its subject binding is established
 * by the RTS facade when the entity is created; systems advance this component
 * with the same injected SimulationStep used by the rest of the RTS world.
 */
class RTSEffectComponent final {
public:
    /** @brief Bind the owning entity subject before applying effects. */
    [[nodiscard]] eve::Result<void> bindSubject(eve::SubjectRef subject);
    /** @brief Apply a typed RTS effect to this entity's canonical lifecycle. */
    [[nodiscard]] eve::Result<effects::EffectHandle> apply(
        const RTSEffectDefinition& definition);
    /** @brief Remove one generation-qualified effect handle. */
    [[nodiscard]] eve::Result<void> remove(effects::EffectHandle handle);
    /** @brief Advance this entity's effect lifecycle and settle domain state. */
    [[nodiscard]] eve::Result<RTSEffectUpdate> advance(const eve::SimulationStep& step);
    /** @brief Return active effect count. */
    [[nodiscard]] std::size_t count() const noexcept { return adapter_.count(); }
    /** @brief Return the mutable RTS target state owned by this component. */
    [[nodiscard]] RTSEffectTarget& target() noexcept { return adapter_.target(); }
    /** @brief Return the read-only RTS target state owned by this component. */
    [[nodiscard]] const RTSEffectTarget& target() const noexcept { return adapter_.target(); }

private:
    RTSEffectAdapter adapter_;
    eve::SubjectRef subject_;
};

}  // namespace eve::rts
