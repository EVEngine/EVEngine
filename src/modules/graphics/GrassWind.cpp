#include "graphics/GrassWind.h"
#include <array>
#include <string>
#include "common/Assert.h"
#include "graphics/Shader.h"
#include "graphics/VegetationWind.h"
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
