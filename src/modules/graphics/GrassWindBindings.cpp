#include "common/SquirrelBinding.h"
#include "graphics/GrassWind.h"
#include "graphics/Shader.h"
#include "graphics/TreeWind.h"
#include "graphics/VegetationWind.h"
namespace eve::graphics {
namespace {
Result<void> missing() {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "grass.wind: non-null shader, state and profile required"));
}
}  // namespace
void exposeGrassWindBindings(ssq::Table& table) {
    auto state = table.addClass("VegetationWindState", ssq::Class::Ctor<VegetationWindState()>());
    state.addVar("strength", &VegetationWindState::strength);
    state.addVar("phase", &VegetationWindState::phase);
    state.addVar("updatePhase", &VegetationWindState::updatePhase);
    state.addFunc("setDirection", [](VegetationWindState* s, float x, float y, float z) { s->direction = {x, y, z}; });
    state.addFunc("getDirectionX", [](const VegetationWindState* s) { return s->direction.x; });
    state.addFunc("getDirectionY", [](const VegetationWindState* s) { return s->direction.y; });
    state.addFunc("getDirectionZ", [](const VegetationWindState* s) { return s->direction.z; });
    auto audioState = table.addClass("VegetationWindAudioState", ssq::Class::Ctor<VegetationWindAudioState()>());
    audioState.addVar("currentWindSpeed", &VegetationWindAudioState::currentWindSpeed);
    audioState.addVar("anchorVolume", &VegetationWindAudioState::anchorVolume);
    audioState.addVar("volume", &VegetationWindAudioState::volume);
    audioState.addVar("processing", &VegetationWindAudioState::processing);
    audioState.addVar("playing", &VegetationWindAudioState::playing);
    auto profile = table.addClass("VegetationWindProfile", ssq::Class::Ctor<VegetationWindProfile()>());
    profile.addVar("maximumDistance", &VegetationWindProfile::maximumDistance);
    profile.addVar("enabled", &VegetationWindProfile::enabled);
    profile.addVar("billboard", &VegetationWindProfile::billboard);
    profile.addVar("alphaTest", &VegetationWindProfile::alphaTest);
    profile.addFunc("setFlex", [](VegetationWindProfile* p, float x, float y, float z) { p->flex = {x, y, z}; });
    profile.addFunc("setFrequency",
                    [](VegetationWindProfile* p, float x, float y, float z) { p->frequency = {x, y, z}; });
    auto tree = table.addClass("TreeWindProfile", ssq::Class::Ctor<TreeWindProfile()>());
    tree.addVar("bendFactor", &TreeWindProfile::bendFactor);
    tree.addFunc("setDimensions", [](TreeWindProfile* p, float width, float height) {
        p->widthHeight = {width, height};
    });
    table.addFunc("advanceVegetationWind", [vm = table.getHandle()](VegetationWindState* s, float x, float y, float z,
                                                                    float strength, float dt) {
        auto result = s ? advanceVegetationWind(*s, {x, y, z}, strength, dt) : missing();
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("initializeVegetationWind",
                  [vm = table.getHandle()](VegetationWindState* s, float x, float y, float z, float strength) {
                      auto result = s ? initializeVegetationWind(*s, {x, y, z}, strength) : missing();
                      return eve::script::projectResult(vm, std::move(result));
                  });
    table.addFunc("applyGrassWind", [vm = table.getHandle()](Shader* shader, const VegetationWindState* s,
                                                             const VegetationWindProfile* p, float seconds) {
        auto result = shader && s && p ? applyGrassWind(*shader, *s, *p, seconds) : missing();
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyTreeWind",
                  [vm = table.getHandle()](Shader* shader, const VegetationWindState* s,
                                           const VegetationWindProfile* p, const TreeWindProfile* treeProfile,
                                           float seconds) {
                      auto result = shader && s && p && treeProfile
                                        ? applyTreeWind(*shader, *s, *p, *treeProfile, seconds)
                                        : missing();
                      return eve::script::projectResult(vm, std::move(result));
                  });
    table.addFunc("advanceVegetationWindAudio",
                  [vm = table.getHandle()](VegetationWindAudioState* state, float strength, float transitionTime,
                                           float dt, bool enabled, bool clipAvailable) {
                      auto result = state ? advanceVegetationWindAudio(*state, strength, transitionTime, dt, enabled,
                                                                      clipAvailable)
                                          : missing();
                      return eve::script::projectResult(vm, std::move(result));
                  });
}
}  // namespace eve::graphics
