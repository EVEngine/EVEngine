#include "procgen/heightmap/CliffDressing.h"

#include <cmath>

namespace eve::procgen {

Result<int> configureCliffRockScatterDefaults(TerrainObjectPlacementSettings& settings, float originX, float originZ,
                                              float width, float depth, float heightScale, std::uint32_t seed,
                                              float spacing, float minimumFitness, const std::string& asset) {
    if (!std::isfinite(originX) || !std::isfinite(originZ) || !std::isfinite(width) || width <= 0.f ||
        !std::isfinite(depth) || depth <= 0.f || !std::isfinite(heightScale) || heightScale < 0.f ||
        !std::isfinite(spacing) || spacing <= 0.f || !std::isfinite(minimumFitness) || minimumFitness < 0.f ||
        minimumFitness > 1.f || asset.empty())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "cliff.dressing: finite positive domain and fitness required"));

    settings                        = TerrainObjectPlacementSettings{};
    settings.originX                = originX;
    settings.originZ                = originZ;
    settings.width                  = width;
    settings.depth                  = depth;
    settings.heightScale            = heightScale;
    settings.spacing                = spacing;
    settings.spawnDensity           = 0.85f;
    settings.jitterPercent          = 0.55f;
    settings.minimumFitness         = minimumFitness;
    settings.minimumInstanceFitness = minimumFitness;
    settings.seed                   = static_cast<std::int32_t>(seed);
    settings.namespaceId            = 0xC11FF001ull ^ (std::uint64_t(seed) << 1);
    settings.prototype              = "cliff.rock";
    settings.maxPoints              = 4096;
    settings.boundsCollisionCheck   = false;

    TerrainObjectInstanceSettings instance;
    instance.asset             = asset;
    instance.minimumInstances  = 1;
    instance.maximumInstances  = 1;
    instance.scaleMode         = TerrainObjectScaleMode::Random;
    instance.minimumScale      = 0.65f;
    instance.maximumScale      = 1.35f;
    instance.minimumRotationY  = 0.f;
    instance.maximumRotationY  = 360.f;
    instance.rotateToSlope     = true;
    instance.yOffsetAlongSlope = true;
    instance.minimumOffsetY    = -0.15f;
    instance.maximumOffsetY    = 0.05f;
    auto added                 = settings.addInstance(instance);
    if (!added.ok()) return added;
    return Result<int>::success(added.value());
}

}  // namespace eve::procgen
