#include "graphics/vulkan/Graphics.h"

namespace eve::graphics::vulkan {
Result<void> Graphics::setSceneToneMapping(SceneToneMapping mode) {
    if (mode != SceneToneMapping::None && mode != SceneToneMapping::Aces)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown scene tone mapping mode", "graphics.presentation"));
    sceneToneMapping_ = mode;
    return Result<void>::success();
}
}  // namespace eve::graphics::vulkan
