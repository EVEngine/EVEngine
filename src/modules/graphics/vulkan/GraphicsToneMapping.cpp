#include "graphics/vulkan/Graphics.h"

#include <cmath>

namespace eve::graphics::vulkan {
Result<void> Graphics::setSceneToneMapping(SceneToneMapping mode) {
    if (mode != SceneToneMapping::None && mode != SceneToneMapping::Aces && mode != SceneToneMapping::Filmic)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown scene tone mapping mode", "graphics.presentation"));
    sceneToneMapping_ = mode;
    return Result<void>::success();
}
Result<void> Graphics::setScenePhotographicVignette(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0 || intensity > 1)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Photographic vignette intensity must be finite and in [0,1]",
                                                       "graphics.presentation"));
    scenePhotographicVignette_ = intensity;
    return Result<void>::success();
}
}  // namespace eve::graphics::vulkan
