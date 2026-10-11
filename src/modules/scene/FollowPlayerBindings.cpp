#include "scene/FollowPlayer.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

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
                  [vm = table.getHandle()](FollowPlayerOutput* out,
                                           FollowPlayerSettings* cfg,
                                           FollowPlayerInput* in) {
        return eve::script::projectResult(vm, evaluateFollowPlayer(out, cfg, in));
    });
}

}  // namespace eve::scene
