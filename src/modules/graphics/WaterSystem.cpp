#include "graphics/WaterSystem.h"

#include <cmath>

namespace eve::graphics {
namespace {
bool finite3(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Result<void> validate(const WaterSystemSettings& settings, const WaterSceneConditions& scene) {
    if ((settings.autoUpdateMode != WaterAutoUpdateMode::Interval &&
         settings.autoUpdateMode != WaterAutoUpdateMode::SceneConditions) ||
        !std::isfinite(settings.refreshRate) || settings.refreshRate <= 0.0F || !finite3(scene.sunColor) ||
        !finite3(scene.sunDirection) || !std::isfinite(scene.sunIntensity) ||
        (scene.timeAvailable && (scene.hour < 0 || scene.hour > 23 || scene.minute < 0 || scene.minute > 59))) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "water.system: positive refresh rate, finite lighting and valid optional time are required"));
    }
    return Result<void>::success();
}

bool changed(const WaterSceneConditions& lhs, const WaterSceneConditions& rhs) {
    if (lhs.sunAvailable != rhs.sunAvailable || lhs.timeAvailable != rhs.timeAvailable) return true;
    if (rhs.sunAvailable && (lhs.sunColor != rhs.sunColor || lhs.sunDirection != rhs.sunDirection ||
                             lhs.sunIntensity != rhs.sunIntensity))
        return true;
    return rhs.timeAvailable && (lhs.hour != rhs.hour || lhs.minute != rhs.minute);
}
}  // namespace

Result<void> initializeWaterSystem(WaterSystemState& state, const WaterSystemSettings& settings,
                                   const WaterSceneConditions& scene, int initialSceneCheckFrames) {
    auto valid = validate(settings, scene);
    if (!valid) return valid;
    if (initialSceneCheckFrames < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "water.system: scene check delay must be nonnegative"));
    WaterSystemState candidate = state;
    candidate.observed = scene;
    candidate.positionY = candidate.seaLevel;
    candidate.refreshRemaining = settings.refreshRate;
    candidate.sceneCheckFrames = initialSceneCheckFrames;
    candidate.initialized = true;
    state = candidate;
    return Result<void>::success();
}

Result<bool> advanceWaterSystem(WaterSystemState& state, const WaterSystemSettings& settings,
                                const WaterSceneConditions& scene, float playerX, float playerZ, float dt,
                                int nextSceneCheckFrames) {
    auto valid = validate(settings, scene);
    if (!valid) return Result<bool>::failure(valid.status());
    if (!state.initialized || !std::isfinite(state.seaLevel) || !std::isfinite(state.refreshRemaining) ||
        !std::isfinite(playerX) || !std::isfinite(playerZ) || !std::isfinite(dt) || dt < 0.0F ||
        nextSceneCheckFrames <= 0) {
        return Result<bool>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "water.system: initialized finite state, nonnegative dt and positive next delay are required"));
    }

    WaterSystemState candidate = state;
    candidate.positionY = candidate.seaLevel;
    if (settings.infiniteMode) {
        candidate.positionX = playerX;
        candidate.positionZ = playerZ;
    }

    bool refresh = false;
    if (settings.autoRefresh) {
        if (settings.autoUpdateMode == WaterAutoUpdateMode::SceneConditions) {
            if (candidate.sceneCheckFrames <= 0 && changed(candidate.observed, scene)) {
                candidate.observed = scene;
                candidate.sceneCheckFrames = nextSceneCheckFrames;
                refresh = true;
            }
        } else {
            candidate.refreshRemaining -= dt;
            if (candidate.refreshRemaining < 0.0F) {
                candidate.refreshRemaining = settings.refreshRate;
                if (settings.ignoreSceneConditions) {
                    refresh = true;
                } else if (changed(candidate.observed, scene)) {
                    candidate.observed = scene;
                    refresh = true;
                }
            }
        }
    }
    --candidate.sceneCheckFrames;
    if (refresh) ++candidate.refreshRevision;
    state = candidate;
    return Result<bool>::success(refresh);
}

Result<bool> updateWaterSeaLevel(WaterSystemState& state, float seaLevel, bool regenerateReflections) {
    if (!state.initialized || !std::isfinite(seaLevel))
        return Result<bool>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.system: initialized state and finite sea level are required"));
    if (state.seaLevel == seaLevel) return Result<bool>::success(false);
    WaterSystemState candidate = state;
    candidate.seaLevel = seaLevel;
    candidate.positionY = seaLevel;
    if (regenerateReflections) ++candidate.refreshRevision;
    state = candidate;
    return Result<bool>::success(true);
}

}  // namespace eve::graphics
