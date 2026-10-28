#include "scene/FollowPlayer.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"

// FollowPlayer.cpp
namespace eve::scene {
namespace {
bool finite(float value) { return std::isfinite(value); }
}  // namespace

Result<void> evaluateFollowPlayer(FollowPlayerOutput* output, const FollowPlayerSettings* settings,
                                  const FollowPlayerInput* input) {
    if (!output || !settings || !input || !finite(settings->offsetX) || !finite(settings->offsetY) ||
        !finite(settings->offsetZ) || !finite(settings->scaleX) || !finite(settings->scaleY) ||
        !finite(settings->scaleZ) || !finite(input->playerX) || !finite(input->playerY) || !finite(input->playerZ))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "scene.followPlayer: objects and finite transform values required"));

    FollowPlayerOutput candidate;
    candidate.applyScale    = settings->useScale;
    candidate.scaleX        = settings->scaleX;
    candidate.scaleY        = settings->scaleY;
    candidate.scaleZ        = settings->scaleZ;
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

// FollowPlayerBindings.cpp
namespace eve::scene {

void exposeFollowPlayerBindings(ssq::Table& table) {
    auto settings = table.addClass("FollowPlayerSettings", ssq::Class::Ctor<FollowPlayerSettings()>());
    settings.addVar("followPlayer", &FollowPlayerSettings::followPlayer);
    settings.addVar("waterObject", &FollowPlayerSettings::waterObject);
    settings.addVar("useOffset", &FollowPlayerSettings::useOffset);
    settings.addVar("offsetX", &FollowPlayerSettings::offsetX);
    settings.addVar("offsetY", &FollowPlayerSettings::offsetY);
    settings.addVar("offsetZ", &FollowPlayerSettings::offsetZ);
    settings.addVar("useScale", &FollowPlayerSettings::useScale);
    settings.addVar("scaleX", &FollowPlayerSettings::scaleX);
    settings.addVar("scaleY", &FollowPlayerSettings::scaleY);
    settings.addVar("scaleZ", &FollowPlayerSettings::scaleZ);

    auto input = table.addClass("FollowPlayerInput", ssq::Class::Ctor<FollowPlayerInput()>());
    input.addVar("hasPlayer", &FollowPlayerInput::hasPlayer);
    input.addVar("playerX", &FollowPlayerInput::playerX);
    input.addVar("playerY", &FollowPlayerInput::playerY);
    input.addVar("playerZ", &FollowPlayerInput::playerZ);

    auto output = table.addClass("FollowPlayerOutput", ssq::Class::Ctor<FollowPlayerOutput()>());
    output.addVar("applyPosition", &FollowPlayerOutput::applyPosition);
    output.addVar("x", &FollowPlayerOutput::x);
    output.addVar("y", &FollowPlayerOutput::y);
    output.addVar("z", &FollowPlayerOutput::z);
    output.addVar("applyScale", &FollowPlayerOutput::applyScale);
    output.addVar("scaleX", &FollowPlayerOutput::scaleX);
    output.addVar("scaleY", &FollowPlayerOutput::scaleY);
    output.addVar("scaleZ", &FollowPlayerOutput::scaleZ);

    table.addFunc("evaluateFollowPlayer",
                  [vm = table.getHandle()](FollowPlayerOutput* out, FollowPlayerSettings* cfg, FollowPlayerInput* in) {
                      return eve::script::projectResult(vm, evaluateFollowPlayer(out, cfg, in));
                  });
}

}  // namespace eve::scene
