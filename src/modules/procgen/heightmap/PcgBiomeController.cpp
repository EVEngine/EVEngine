#include "procgen/heightmap/PcgBiomeController.h"

#include <cmath>

namespace eve::procgen {
namespace {
bool finiteBounds(double minX, double minY, double minZ, double sizeX, double sizeY, double sizeZ) {
    return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(minZ) && std::isfinite(sizeX) &&
           std::isfinite(sizeY) && std::isfinite(sizeZ) && sizeX >= 0.0 && sizeY >= 0.0 && sizeZ >= 0.0;
}

bool contains(const PcgBiomeLoadingBounds& bounds, double x, double y, double z) noexcept {
    if (bounds.sizeX <= 0.0 || bounds.sizeY <= 0.0 || bounds.sizeZ <= 0.0) return false;
    return std::abs(x - bounds.centerX) <= bounds.sizeX * 0.5 &&
           std::abs(y - bounds.centerY) <= bounds.sizeY * 0.5 &&
           std::abs(z - bounds.centerZ) <= bounds.sizeZ * 0.5;
}

Result<void> invalidController(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, {}, {},
                                                    "procgen.pcgBiomeController"));
}
}  // namespace

Result<void> PcgBiomeController::publish(double x, double y, double z, double range,
                                          double impostorLoadingRange, PcgBiomeLoadMode mode) {
    const int modeValue = static_cast<int>(mode);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(range) ||
        !std::isfinite(impostorLoadingRange) || range < 0.0 || impostorLoadingRange < 0.0 ||
        modeValue < static_cast<int>(PcgBiomeLoadMode::Disabled) ||
        modeValue > static_cast<int>(PcgBiomeLoadMode::RuntimeAlways)) {
        return invalidController("finite position, non-negative ranges, and a valid load mode are required");
    }

    PcgBiomeLoadingBounds regular;
    PcgBiomeLoadingBounds impostor;
    if (mode != PcgBiomeLoadMode::Disabled) {
        const double width = range * 2.0 - 0.5;
        if (width < 0.0) return invalidController("enabled biome range must be at least 0.25 world units");
        regular = {x, y, z, width, width, width};
        impostor.centerX = x;
        impostor.centerY = y;
        impostor.centerZ = z;
        if (impostorLoadingRange > 0.0) {
            const double impostorWidth = width + impostorLoadingRange;
            impostor.sizeX = impostorWidth;
            impostor.sizeY = impostorWidth;
            impostor.sizeZ = impostorWidth;
        }
    }

    x_ = x;
    y_ = y;
    z_ = z;
    range_ = range;
    impostorLoadingRange_ = impostorLoadingRange;
    mode_ = mode;
    regular_ = regular;
    impostor_ = impostor;
    return Result<void>::success();
}

Result<void> PcgBiomeController::configure(double x, double y, double z, double range,
                                            double impostorLoadingRange, PcgBiomeLoadMode mode) {
    return publish(x, y, z, range, impostorLoadingRange, mode);
}

Result<void> PcgBiomeController::fitToTerrain(double minX, double originY, double minZ,
                                               double sizeX, double sizeY, double sizeZ) {
    if (!finiteBounds(minX, originY, minZ, sizeX, sizeY, sizeZ))
        return invalidController("finite terrain origin and non-negative terrain size are required");
    return publish(minX + sizeX * 0.5, originY, minZ + sizeZ * 0.5, sizeX * 0.5,
                   impostorLoadingRange_, mode_);
}

Result<void> PcgBiomeController::fitToAllTerrains(double minX, double minY, double minZ,
                                                   double sizeX, double sizeY, double sizeZ) {
    if (!finiteBounds(minX, minY, minZ, sizeX, sizeY, sizeZ))
        return invalidController("finite world bounds and non-negative world size are required");
    return publish(minX + sizeX * 0.5, minY + sizeY * 0.5, minZ + sizeZ * 0.5, sizeX * 0.5,
                   impostorLoadingRange_, mode_);
}

PcgTerrainStreamingTier PcgBiomeController::tierAt(double x, double y, double z) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || mode_ == PcgBiomeLoadMode::Disabled)
        return PcgTerrainStreamingTier::Unloaded;
    if (contains(regular_, x, y, z)) return PcgTerrainStreamingTier::Regular;
    if (contains(impostor_, x, y, z)) return PcgTerrainStreamingTier::Impostor;
    return PcgTerrainStreamingTier::Unloaded;
}

}  // namespace eve::procgen
