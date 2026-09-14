#include "physics/TerrainLoadGravity.h"

#include "physics/Body3D.h"

#include <cmath>

namespace eve::physics {
namespace {
template <typename T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
}

Result<void> beginTerrainLoadGravity(TerrainLoadGravityState* state, Body3D* body,
                                     bool terrainFound) {
    if (!state || !body || !body->isValid())
        return invalid<void>("physics.terrainLoadGravity.begin: live state and body required");
    if (state->monitoring_ || state->completed_)
        return invalid<void>("physics.terrainLoadGravity.begin: state already initialized");
    state->gravityScale_ = body->getGravityScale();
    if (!terrainFound) {
        state->completed_ = true;
        return Result<void>::success();
    }
    body->setGravityScale(0.f);
    state->monitoring_ = true;
    return Result<void>::success();
}

Result<bool> advanceTerrainLoadGravity(TerrainLoadGravityState* state, Body3D* body,
                                       bool terrainLoaded, float deltaSeconds,
                                       float activationDelay) {
    if (!state || !body || !body->isValid() || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.f || !std::isfinite(activationDelay) || activationDelay < 0.f)
        return invalid<bool>(
            "physics.terrainLoadGravity.advance: live objects and non-negative finite time required");
    if (!state->monitoring_) return Result<bool>::success(false);
    if (!state->activationScheduled_) {
        if (!terrainLoaded) {
            body->setGravityScale(0.f);
            return Result<bool>::success(false);
        }
        state->activationScheduled_ = true;
    }
    state->delayElapsed_ += deltaSeconds;
    if (state->delayElapsed_ + 1e-6f < activationDelay)
        return Result<bool>::success(false);
    body->setGravityScale(state->gravityScale_);
    state->monitoring_ = false;
    state->completed_ = true;
    return Result<bool>::success(true);
}

}  // namespace eve::physics
