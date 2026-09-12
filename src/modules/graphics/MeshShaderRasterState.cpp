#include "graphics/MeshShaderRasterState.h"
#include <cmath>

namespace eve::graphics {
Result<void> validateMeshShaderRasterState(const MeshShaderRasterState& state) {
    if ((state.depthCompare != MeshDepthCompare::Less && state.depthCompare != MeshDepthCompare::LessEqual &&
         state.depthCompare != MeshDepthCompare::Always) ||
        state.colorWriteMask > 15 || !std::isfinite(state.depthBiasConstant) || !std::isfinite(state.depthBiasSlope))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid custom mesh depth comparison, bias or color mask",
                                                       "shader.raster"));
    return Result<void>::success();
}
}  // namespace eve::graphics
