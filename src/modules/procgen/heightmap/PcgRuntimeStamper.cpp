#include "procgen/heightmap/PcgRuntimeStamper.h"

#include "procgen/heightmap/TerrainStamp.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {
template <typename T> Result<T> invalid(const std::string& message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, {}, {},
                                                 "procgen.pcgRuntimeStamper"));
}

bool validRaster(const Heightmap& map) {
    return map.getWidth() > 0 && map.getHeight() > 0 &&
           std::all_of(map.data().begin(), map.data().end(), [](float value) { return std::isfinite(value); });
}
}  // namespace

Result<void> PcgRuntimeStamper::configure(std::string stampAddress, bool showGui, bool showDebug) {
    std::replace(stampAddress.begin(), stampAddress.end(), '\\', '/');
    if (stampAddress.empty()) return invalid<void>("stamp resource address must not be empty");
    address_ = std::move(stampAddress);
    showGui_ = showGui;
    showDebug_ = showDebug;
    stamp_ = {};
    progress_.clear();
    status_ = PcgRuntimeStampStatus::AwaitingResource;
    return Result<void>::success();
}

Result<void> PcgRuntimeStamper::loadStamp(const Heightmap& stamp, const std::string& resourceName) {
    if (status_ == PcgRuntimeStampStatus::Idle)
        return invalid<void>("configure must be called before loading a stamp");
    if (!validRaster(stamp)) {
        progress_ = "Failed to load stamp";
        status_ = PcgRuntimeStampStatus::Failed;
        return invalid<void>("stamp resource must be a finite nonempty raster");
    }
    stamp_ = stamp;
    progress_ = "Loaded stamp at " + resourceName;
    status_ = PcgRuntimeStampStatus::Loaded;
    return Result<void>::success();
}

Result<void> PcgRuntimeStamper::reportMissingStamp() {
    if (status_ == PcgRuntimeStampStatus::Idle)
        return invalid<void>("configure must be called before reporting a missing stamp");
    progress_ = "Failed to load stamp at " + address_;
    status_ = PcgRuntimeStampStatus::Failed;
    return invalid<void>(progress_);
}

Result<int> PcgRuntimeStamper::execute(Heightmap& target, double originX, double originZ,
                                        double spacingX, double spacingZ) {
    if (status_ != PcgRuntimeStampStatus::Loaded || !validRaster(stamp_))
        return invalid<int>("a valid stamp resource must be loaded before execution");
    if (!validRaster(target) || !std::isfinite(originX) || !std::isfinite(originZ) ||
        !std::isfinite(spacingX) || !std::isfinite(spacingZ) || spacingX <= 0.0 || spacingZ <= 0.0)
        return invalid<int>("finite nonempty terrain and positive grid spacing are required");

    Heightmap candidate = target;
    std::fill(candidate.data().begin(), candidate.data().end(), 0.0F);
    Heightmap distanceMask(stamp_.getWidth(), stamp_.getHeight());
    const double halfX = std::max(0.5, double(stamp_.getWidth() - 1) * 0.5);
    const double halfY = std::max(0.5, double(stamp_.getHeight() - 1) * 0.5);
    for (int y = 0; y < distanceMask.getHeight(); ++y) {
        for (int x = 0; x < distanceMask.getWidth(); ++x) {
            const double nx = (x - double(stamp_.getWidth() - 1) * 0.5) / halfX;
            const double ny = (y - double(stamp_.getHeight() - 1) * 0.5) / halfY;
            distanceMask.setHeight(x, y, float(std::max(0.0, 1.0 - std::sqrt(nx * nx + ny * ny))));
        }
    }
    Heightmap one(1, 1);
    one.setHeight(0, 0, 1.0F);
    TerrainStampSettings settings;
    settings.originX = originX;
    settings.originZ = originZ;
    settings.spacingX = spacingX;
    settings.spacingZ = spacingZ;
    settings.centerX = originX + double(target.getWidth() - 1) * spacingX * 0.5;
    settings.centerZ = originZ + double(target.getHeight() - 1) * spacingZ * 0.5;
    settings.width = std::max(spacingX, double(target.getWidth() - 1) * spacingX);
    settings.depth = std::max(spacingZ, double(target.getHeight() - 1) * spacingZ);
    settings.rotation = 0.0;
    settings.amplitude = 6.0F;
    settings.operation = TerrainStampOperation::Set;
    auto result = applyTerrainStamp(candidate, stamp_, settings, one, distanceMask);
    if (!result.ok()) {
        progress_ = "Failed to load stamp";
        status_ = PcgRuntimeStampStatus::Failed;
        return result;
    }
    target = std::move(candidate);
    progress_ = "Stamp progress: 1";
    status_ = PcgRuntimeStampStatus::Completed;
    return result;
}

Result<void> PcgRuntimeStamper::updateLayout(double screenWidth, double screenHeight) {
    if (!std::isfinite(screenWidth) || !std::isfinite(screenHeight) || screenWidth < 0.0 || screenHeight < 0.0)
        return invalid<void>("finite non-negative screen dimensions are required");
    labelX_ = screenWidth * 0.5;
    labelY_ = screenHeight - 20.0;
    return Result<void>::success();
}

}  // namespace eve::procgen
