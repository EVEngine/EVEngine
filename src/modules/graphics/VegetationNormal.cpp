#include "graphics/VegetationNormal.h"
#include <cmath>

namespace eve::graphics {
Result<glm::vec3> decodeVegetationNormal(glm::vec4 sample, VegetationNormalEncoding encoding, float strength) {
    auto invalid = [] {
        return Result<glm::vec3>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "vegetation normal requires finite unit channels, a known encoding and strength in [-8,8]", {}, {},
            "graphics.vegetation.normal"));
    };
    if (!std::isfinite(strength) || strength < -8.f || strength > 8.f) return invalid();
    for (unsigned c = 0; c < 4; ++c)
        if (!std::isfinite(sample[c]) || sample[c] < 0.f || sample[c] > 1.f) return invalid();
    float x;
    switch (encoding) {
        case VegetationNormalEncoding::RedGreen: x = sample.r; break;
        case VegetationNormalEncoding::RedAlphaGreen: x = sample.r * sample.a; break;
        case VegetationNormalEncoding::AlphaGreen: x = sample.a; break;
        default: return invalid();
    }
    return Result<glm::vec3>::success(glm::vec3((2.f * x - 1.f) * strength, (2.f * sample.g - 1.f) * strength, 1.f));
}
}  // namespace eve::graphics
