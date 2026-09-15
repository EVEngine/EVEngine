#include "scene/FollowPlayer.h"

#include <cmath>

namespace eve::scene {
namespace {
bool finite(float value) { return std::isfinite(value); }
}

Result<void> evaluateFollowPlayer(FollowPlayerOutput* output,
                                  const FollowPlayerSettings* settings,
                                  const FollowPlayerInput* input) {
    if (!output || !settings || !input || !finite(settings->offsetX) ||
        !finite(settings->offsetY) || !finite(settings->offsetZ) ||
        !finite(settings->scaleX) || !finite(settings->scaleY) ||
        !finite(settings->scaleZ) || !finite(input->playerX) ||
        !finite(input->playerY) || !finite(input->playerZ))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "scene.followPlayer: objects and finite transform values required"));

    FollowPlayerOutput candidate;
    candidate.applyScale = settings->useScale;
    candidate.scaleX = settings->scaleX;
    candidate.scaleY = settings->scaleY;
    candidate.scaleZ = settings->scaleZ;
    candidate.applyPosition = settings->followPlayer && input->hasPlayer;
    if (candidate.applyPosition) {
        candidate.x = input->playerX;
        candidate.y = input->playerY;
        candidate.z = input->playerZ;
        if (settings->useOffset) {
            candidate.x += settings->offsetX;
            candidate.z -= settings->offsetZ;
            if (settings->waterObject)
                candidate.y += (input->playerY < 1.f ? 70.f : 10.f) - settings->offsetY;
            else
                candidate.y -= settings->offsetY;
        }
    }
    *output = candidate;
    return Result<void>::success();
}

}  // namespace eve::scene
