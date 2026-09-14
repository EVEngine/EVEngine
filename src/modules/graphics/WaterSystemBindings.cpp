#include "graphics/WaterSystem.h"

#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {
namespace {
Result<void> missing() {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "water.system: non-null state, settings and scene required"));
}
}  // namespace

void exposeWaterSystemBindings(ssq::Table& table) {
    auto scene = table.addClass("WaterSceneConditions", ssq::Class::Ctor<WaterSceneConditions()>());
    scene.addVar("sunIntensity", &WaterSceneConditions::sunIntensity);
    scene.addVar("hour", &WaterSceneConditions::hour);
    scene.addVar("minute", &WaterSceneConditions::minute);
    scene.addVar("sunAvailable", &WaterSceneConditions::sunAvailable);
    scene.addVar("timeAvailable", &WaterSceneConditions::timeAvailable);
    scene.addFunc("setSunColor", [](WaterSceneConditions* value, float r, float g, float b) {
        value->sunColor = {r, g, b};
    });
    scene.addFunc("setSunDirection", [](WaterSceneConditions* value, float x, float y, float z) {
        value->sunDirection = {x, y, z};
    });

    auto settings = table.addClass("WaterSystemSettings", ssq::Class::Ctor<WaterSystemSettings()>());
    settings.addVar("refreshRate", &WaterSystemSettings::refreshRate);
    settings.addVar("infiniteMode", &WaterSystemSettings::infiniteMode);
    settings.addVar("autoRefresh", &WaterSystemSettings::autoRefresh);
    settings.addVar("ignoreSceneConditions", &WaterSystemSettings::ignoreSceneConditions);
    settings.addFunc("setAutoUpdateMode", [](WaterSystemSettings* value, int mode) {
        value->autoUpdateMode = static_cast<WaterAutoUpdateMode>(mode);
    });

    auto state = table.addClass("WaterSystemState", ssq::Class::Ctor<WaterSystemState()>());
    state.addVar("seaLevel", &WaterSystemState::seaLevel);
    state.addVar("positionX", &WaterSystemState::positionX);
    state.addVar("positionY", &WaterSystemState::positionY);
    state.addVar("positionZ", &WaterSystemState::positionZ);
    state.addVar("refreshRemaining", &WaterSystemState::refreshRemaining);
    state.addVar("sceneCheckFrames", &WaterSystemState::sceneCheckFrames);
    state.addVar("refreshRevision", &WaterSystemState::refreshRevision);
    state.addVar("initialized", &WaterSystemState::initialized);

    table.addFunc("initializeWaterSystem",
                  [vm = table.getHandle()](WaterSystemState* state, const WaterSystemSettings* settings,
                                           const WaterSceneConditions* scene, int initialDelay) {
                      auto result = state && settings && scene
                                        ? initializeWaterSystem(*state, *settings, *scene, initialDelay)
                                        : missing();
                      return eve::script::projectResult(vm, std::move(result));
                  });
    table.addFunc("advanceWaterSystem",
                  [vm = table.getHandle()](WaterSystemState* state, const WaterSystemSettings* settings,
                                           const WaterSceneConditions* scene, float playerX, float playerZ, float dt,
                                           int nextDelay) {
                      auto result = state && settings && scene
                                        ? advanceWaterSystem(*state, *settings, *scene, playerX, playerZ, dt, nextDelay)
                                        : Result<bool>::failure(missing().status());
                      return eve::script::projectResult(vm, std::move(result), [](bool value) { return value; });
                  });
    table.addFunc("updateWaterSeaLevel", [vm = table.getHandle()](WaterSystemState* state, float seaLevel,
                                                                  bool regenerate) {
        auto result = state ? updateWaterSeaLevel(*state, seaLevel, regenerate)
                            : Result<bool>::failure(missing().status());
        return eve::script::projectResult(vm, std::move(result), [](bool value) { return value; });
    });
}

}  // namespace eve::graphics
