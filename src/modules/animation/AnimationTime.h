#pragma once

#include "common/Time.h"

#include <cmath>
#include <string>
#include <utility>
#include <optional>

namespace eve::animation::detail {

/** @brief Validate a scheduler step before an animation object mutates state. */
inline eve::Result<float> secondsForStep(const eve::SimulationStep& step,
                                         bool hasLastTick,
                                         eve::SimulationTick lastTick,
                                         const char* owner) {
    if (step.delta.nanoseconds() < 0)
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            std::string(owner) + " simulation delta must be non-negative"));
    if (hasLastTick && step.tick <= lastTick)
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict,
            std::string(owner) + " simulation tick must advance monotonically"));

    const float seconds = static_cast<float>(step.delta.seconds());
    if (!std::isfinite(seconds))
        return eve::Result<float>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            std::string(owner) + " simulation delta is outside float range"));
    return eve::Result<float>::success(seconds);
}

/** @brief Convert a legacy seconds call to the next local scheduler step. */
inline eve::Result<eve::SimulationStep> legacyStep(float seconds,
                                                   bool hasLastTick,
                                                   eve::SimulationTick lastTick,
                                                   const char* owner) {
    auto duration = eve::Duration::fromSeconds(seconds);
    if (!duration) return eve::Result<eve::SimulationStep>::failure(duration.status());
    auto nextTick = hasLastTick ? lastTick.incremented()
                                : std::optional<eve::SimulationTick>(eve::SimulationTick(1));
    if (!nextTick)
        return eve::Result<eve::SimulationStep>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            std::string(owner) + " simulation tick overflow"));
    return eve::Result<eve::SimulationStep>::success(
        eve::SimulationStep{*nextTick, std::move(duration).takeValue()});
}

}  // namespace eve::animation::detail
