#include "graphics/Graphics.h"

namespace eve::graphics {
Result<void> Graphics::setSceneToneMapping(SceneToneMapping mode) {
    if (mode != SceneToneMapping::None && mode != SceneToneMapping::Aces)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unknown scene tone mapping mode", "graphics.presentation"));
    if (mode == getSceneToneMapping()) return Result<void>::success();
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                   "This backend does not support changing scene tone mapping",
                                                   "graphics.presentation"));
}
}  // namespace eve::graphics
