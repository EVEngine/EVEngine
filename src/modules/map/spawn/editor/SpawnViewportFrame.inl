#pragma once
// Shared private frame admission for viewport and owning workspace; no second camera schema.
#include <cmath>
#include "map/spawn/editor/SpawnMapViewport.h"
namespace eve::spawn_editor::detail {
inline Result<void> validateViewportFrame(MapViewportFrame frame) {
    if (!std::isfinite(frame.screenX) || !std::isfinite(frame.screenY) || !std::isfinite(frame.panX) ||
        !std::isfinite(frame.panY) || !std::isfinite(frame.zoom) || frame.zoom <= 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid map viewport frame", "spawn.viewport"));
    return Result<void>::success();
}
}  // namespace eve::spawn_editor::detail
