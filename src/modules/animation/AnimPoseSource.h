#pragma once

#include "animation/AnimPose.h"
#include "common/Time.h"

#include <string>

namespace eve::animation {

class AnimSkeleton;

/**
 * @brief Borrowed pose provider consumed by AnimLayerMixer.
 *
 * Implementations are not owned by the mixer. Callers must keep the source
 * alive for as long as it remains attached as a base or layer. `advance`
 * must be invoked by the mixer (or an equivalent evaluation owner); do not
 * also advance the same source with the same tick elsewhere.
 */
class IAnimPoseSource {
public:
    virtual ~IAnimPoseSource() = default;

    /**
     * @brief Skeleton this source evaluates against; never null while attached.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    [[nodiscard]] virtual AnimSkeleton* getSkeleton() const = 0;

    /**
     * @brief Most recent local pose; valid after a successful advance.
     * @ownership Borrowed
     * @lifetime Valid while the owning animation object remains alive; do not retain across destruction.
     */
    [[nodiscard]] virtual AnimPose* getPose() = 0;

    /** @brief Consume one scheduler step and refresh getPose(). */
    [[nodiscard]] virtual eve::Result<void> advance(const eve::SimulationStep& step) = 0;

    /** @brief Whether advance has consumed at least one checked step. */
    [[nodiscard]] virtual bool hasCurrentTick() const noexcept = 0;

    /** @brief Last scheduler tick consumed by advance. */
    [[nodiscard]] virtual eve::SimulationTick currentTick() const noexcept = 0;

    /** @brief Events emitted by the most recent advance; default is none. */
    [[nodiscard]] virtual int         getEventCount() const { return 0; }
    [[nodiscard]] virtual std::string getEventName(int /*index*/) const { return {}; }
    [[nodiscard]] virtual std::string getEventPayload(int /*index*/) const { return {}; }
};

/**
 * @brief Reference pose used when converting an additive sample into a delta.
 *
 * - BindPose: sample is an absolute local pose; delta = sample ⊖ bind (AnimLayerMixer default).
 * - Identity: sample is already a delta in local space (AnimGraph additive default).
 */
enum class AnimAdditiveReference {
    BindPose,
    Identity,
};

}  // namespace eve::animation
