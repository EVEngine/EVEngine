#include "graphics/PbrSurface.h"
#include <cmath>
namespace eve::graphics {
Result<void> validatePbrSurface(const PbrSurface& s) {
    auto fail = [] {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid PBR surface factors or sampler", {}, {}, "graphics.material"));
    };
    auto unit     = [](float x) { return std::isfinite(x) && x >= 0 && x <= 1; };
    auto positive = [](float x) { return std::isfinite(x) && x >= 0; };
    if (!unit(s.occlusionStrength) || !unit(s.specularFactor) || !unit(s.anisotropyStrength) ||
        !unit(s.clearcoatFactor) || !unit(s.clearcoatRoughness) || !positive(s.emissiveStrength) ||
        !std::isfinite(s.normalScale) || !std::isfinite(s.clearcoatNormalScale) ||
        !std::isfinite(s.anisotropyRotation) || !std::isfinite(s.ior) || (s.ior != 0 && s.ior < 1))
        return fail();
    for (float v : s.emissive)
        if (!positive(v)) return fail();
    for (float v : s.specularColor)
        if (!positive(v)) return fail();
    for (const auto& b : s.textures) {
        for (float v : b.offset)
            if (!std::isfinite(v)) return fail();
        for (float v : b.scale)
            if (!std::isfinite(v)) return fail();
        auto wrap = [](uint32_t v) { return v == 10497 || v == 33071 || v == 33648; };
        if (!std::isfinite(b.rotation) || !wrap(b.wrapS) || !wrap(b.wrapT) ||
            (b.magFilter != 9728 && b.magFilter != 9729) ||
            (b.minFilter != 9728 && b.minFilter != 9729 && (b.minFilter < 9984 || b.minFilter > 9987)))
            return fail();
    }
    return Result<void>::success();
}
}  // namespace eve::graphics
