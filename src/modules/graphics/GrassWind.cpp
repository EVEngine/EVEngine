#include "graphics/GrassWind.h"
#include <array>
#include <cmath>
#include <string>
#include "common/Assert.h"
#include "common/SquirrelBinding.h"
#include "graphics/Shader.h"
#include "graphics/TreeWind.h"
#include "graphics/VegetationWind.h"

// GrassWind.cpp
namespace eve::graphics {
Result<void> applyGrassWind(Shader& shader, const VegetationWindState& state, const VegetationWindProfile& profile,
                            double seconds) {
    auto packet = packVegetationWind(state, profile, seconds);
    if (!packet.ok()) return Result<void>::failure(packet.status());
    // Construct names before publication. Existing uniform writes only check schema and copy bytes.
    static const std::array<std::string, 5> names = {"windGlobals", "windPhaseDistance", "windFlex", "windFrequency",
                                                     "windSineTime"};
    constexpr std::array<int, 5>            offsets{18, 22, 24, 27, 30};
    constexpr std::array<int, 5>            sizes{4, 2, 3, 3, 2};
    for (size_t i = 0; i < names.size(); ++i) {
        if (shader.usedFloats() != 32 || !shader.hasUniform(names[i]) ||
            shader.getUniformIndex(names[i]) != offsets[i] || shader.getUniformFloatCount(names[i]) != sizes[i])
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "grass.wind: canonical grass wind uniform layout required"));
    }
    for (size_t i = 0; i < names.size(); ++i) {
        const size_t bytes   = size_t(sizes[i]) * sizeof(float);
        const int    written = shader.sendToVar(names[i], packet.value().data() + offsets[i] - 18, bytes);
        EV_ASSERT(written == int(bytes));
    }
    return Result<void>::success();
}
}  // namespace eve::graphics

// TreeWind.cpp
namespace eve::graphics {
namespace {}  // namespace

Result<void> applyTreeWind(Shader& shader, const VegetationWindState& state, const VegetationWindProfile& vegetation,
                           const TreeWindProfile& tree, double seconds) {
    if (!std::isfinite(tree.widthHeight.x) || tree.widthHeight.x <= 0 || !std::isfinite(tree.widthHeight.y) ||
        tree.widthHeight.y <= 0 || !std::isfinite(tree.bendFactor) || tree.bendFactor < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "tree.wind: positive finite dimensions and bend required"));
    auto scaled = vegetation;
    scaled.flex *= tree.bendFactor;
    auto packet = packVegetationWind(state, scaled, seconds);
    if (!packet.ok()) return Result<void>::failure(packet.status());
    static const std::array<std::string, 6> names = {"windGlobals",   "windPhaseDistance", "windFlex",
                                                     "windFrequency", "windSineTime",      "treeDimensions"};
    constexpr std::array<int, 6>            offsets{0, 4, 6, 9, 12, 14};
    constexpr std::array<int, 6>            sizes{4, 2, 3, 3, 2, 2};
    for (size_t i = 0; i < names.size(); ++i)
        if (shader.usedFloats() != 16 || !shader.hasUniform(names[i]) ||
            shader.getUniformIndex(names[i]) != offsets[i] || shader.getUniformFloatCount(names[i]) != sizes[i])
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "tree.wind: canonical 16-float shader layout required"));
    std::array<float, 16> values{};
    std::copy(packet.value().begin(), packet.value().end(), values.begin());
    values[14] = tree.widthHeight.x;
    values[15] = tree.widthHeight.y;
    for (size_t i = 0; i < names.size(); ++i) {
        const auto bytes   = size_t(sizes[i]) * sizeof(float);
        const int  written = shader.sendToVar(names[i], values.data() + offsets[i], bytes);
        EV_ASSERT(written == int(bytes));
    }
    return Result<void>::success();
}
}  // namespace eve::graphics

// GrassWindBindings.cpp
namespace eve::graphics {
namespace {}  // namespace
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
    tree.addFunc("setDimensions",
                 [](TreeWindProfile* p, float width, float height) { p->widthHeight = {width, height}; });
    table.addFunc("advanceVegetationWind", [vm = table.getHandle()](VegetationWindState* s, float x, float y, float z,
                                                                    float strength, float dt) {
        auto result =
            s ? advanceVegetationWind(*s, {x, y, z}, strength, dt)
              : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "grass.wind: non-null shader, state and profile required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("initializeVegetationWind", [vm = table.getHandle()](VegetationWindState* s, float x, float y,
                                                                       float z, float strength) {
        auto result =
            s ? initializeVegetationWind(*s, {x, y, z}, strength)
              : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                        "grass.wind: non-null shader, state and profile required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyGrassWind", [vm = table.getHandle()](Shader* shader, const VegetationWindState* s,
                                                             const VegetationWindProfile* p, float seconds) {
        auto result =
            shader && s && p
                ? applyGrassWind(*shader, *s, *p, seconds)
                : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "grass.wind: non-null shader, state and profile required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("applyTreeWind", [vm = table.getHandle()](Shader* shader, const VegetationWindState* s,
                                                            const VegetationWindProfile* p,
                                                            const TreeWindProfile* treeProfile, float seconds) {
        auto result =
            shader && s && p && treeProfile
                ? applyTreeWind(*shader, *s, *p, *treeProfile, seconds)
                : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "grass.wind: non-null shader, state and profile required"));
        return eve::script::projectResult(vm, std::move(result));
    });
    table.addFunc("advanceVegetationWindAudio", [vm = table.getHandle()](VegetationWindAudioState* state,
                                                                         float strength, float transitionTime, float dt,
                                                                         bool enabled, bool clipAvailable) {
        auto result =
            state ? advanceVegetationWindAudio(*state, strength, transitionTime, dt, enabled, clipAvailable)
                  : Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "grass.wind: non-null shader, state and profile required"));
        return eve::script::projectResult(vm, std::move(result));
    });
}
}  // namespace eve::graphics
