#include "graphics/Graphics.h"

#include <cmath>

namespace eve::graphics {
Result<void> Graphics::setSceneToneMapping(SceneToneMapping mode) {
    if (mode != SceneToneMapping::None && mode != SceneToneMapping::Aces && mode != SceneToneMapping::Filmic)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown scene tone mapping mode", "graphics.presentation"));
    if (mode == getSceneToneMapping()) return Result<void>::success();
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                   "This backend does not support changing scene tone mapping",
                                                   "graphics.presentation"));
}
Result<void> Graphics::setScenePhotographicVignette(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0 || intensity > 1)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Photographic vignette intensity must be finite and in [0,1]",
                                                       "graphics.presentation"));
    if (intensity == 0) return Result<void>::success();
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "This backend does not support photographic vignette", "graphics.presentation"));
}
}  // namespace eve::graphics
