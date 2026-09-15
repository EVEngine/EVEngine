#include "graphics/TreeWind.h"
#include <array>
#include <cmath>
#include <string>
#include "common/Assert.h"
#include "graphics/Shader.h"
#include "graphics/VegetationWind.h"

namespace eve::graphics {
namespace {
}  // namespace

Result<void> applyTreeWind(Shader& shader, const VegetationWindState& state,
                           const VegetationWindProfile& vegetation, const TreeWindProfile& tree,
                           double seconds) {
    if (!std::isfinite(tree.widthHeight.x) || tree.widthHeight.x <= 0 ||
        !std::isfinite(tree.widthHeight.y) || tree.widthHeight.y <= 0 ||
        !std::isfinite(tree.bendFactor) || tree.bendFactor < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "tree.wind: positive finite dimensions and bend required"));
    auto scaled = vegetation;
    scaled.flex *= tree.bendFactor;
    auto packet = packVegetationWind(state, scaled, seconds);
    if (!packet.ok()) return Result<void>::failure(packet.status());
    static const std::array<std::string, 6> names = {"windGlobals", "windPhaseDistance", "windFlex",
                                                     "windFrequency", "windSineTime", "treeDimensions"};
    constexpr std::array<int, 6> offsets{0, 4, 6, 9, 12, 14};
    constexpr std::array<int, 6> sizes{4, 2, 3, 3, 2, 2};
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
        const auto bytes = size_t(sizes[i]) * sizeof(float);
        const int written = shader.sendToVar(names[i], values.data() + offsets[i], bytes);
        EV_ASSERT(written == int(bytes));
    }
    return Result<void>::success();
}
}  // namespace eve::graphics
