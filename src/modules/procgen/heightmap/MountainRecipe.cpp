#include "procgen/heightmap/MountainRecipe.h"

#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainSampler.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace eve::procgen {
namespace {

using namespace raster_detail;

struct PresetNoise {
    float frequency = 1.f / 96.f;
    int   octaves   = 5;
    float gain      = 0.42f;
    float ridge     = 0.55f;
    float warp      = 0.28f;
    float exponent  = 1.35f;
    float continent = 1.f;
    float island    = 0.f;
    float coast     = 0.25f;
    float base      = 0.f;
    float amplitude = 1.f;
};

struct PresetErosion {
    ThermalErosionSettings   thermal;
    HydraulicErosionSettings hydraulic;
    FluvialErosionSettings   fluvial;
};

PresetNoise noiseFor(MountainPreset preset) {
    PresetNoise n;
    switch (preset) {
        case MountainPreset::Mesa:
            n.frequency = 1.f / 140.f;
            n.octaves   = 4;
            n.gain      = 0.32f;
            n.ridge     = 0.22f;
            n.warp      = 0.12f;
            n.exponent  = 2.4f;
            n.continent = 0.85f;
            break;
        case MountainPreset::CoastalCliff:
            n.frequency = 1.f / 110.f;
            n.octaves   = 5;
            n.gain      = 0.38f;
            n.ridge     = 0.40f;
            n.warp      = 0.20f;
            n.exponent  = 1.6f;
            n.continent = 0.72f;
            n.island    = 0.45f;
            n.coast     = 0.18f;
            break;
        case MountainPreset::AlpineRidge:
        default: break;
    }
    return n;
}

PresetErosion erosionFor(MountainPreset preset) {
    PresetErosion e;
    // Cliff-friendly: higher talus keeps steep faces; fluvial carves valleys.
    e.thermal   = {28, 0.012f, 0.28f};
    e.hydraulic = {18, 0.008f, 0.10f, 1.5f, 0.10f, 0.12f};
    e.fluvial   = {16, 0.006f, 0.09f, 0.20f, 6.f, 0.03f, 1.f};
    switch (preset) {
        case MountainPreset::Mesa:
            e.thermal.talus      = 0.018f;
            e.thermal.iterations = 22;
            e.fluvial.iterations = 10;
            e.fluvial.incision   = 0.06f;
            break;
        case MountainPreset::CoastalCliff:
            e.thermal.talus      = 0.020f;
            e.thermal.strength   = 0.22f;
            e.hydraulic.erosion  = 0.07f;
            e.fluvial.iterations = 12;
            break;
        case MountainPreset::AlpineRidge:
        default:
            e.thermal.talus = 0.008f;  // Allow steeper alpine faces.
            break;
    }
    return e;
}

Result<int> writeCliffFitness(Heightmap& cliff, const Heightmap& slope, float slopeMin, float slopeMax) {
    if (cliff.getWidth() != slope.getWidth() || cliff.getHeight() != slope.getHeight() || !std::isfinite(slopeMin) ||
        !std::isfinite(slopeMax) || slopeMin > slopeMax)
        return invalid("mountain.recipe: cliff fitness requires matching slope raster and ordered bounds");
    std::vector<float> out(cliff.data().size());
    const float        span = slopeMax - slopeMin;
    for (size_t i = 0; i < out.size(); ++i) {
        const float s = slope.data()[i];
        if (!(span > 0.f)) {
            out[i] = s >= slopeMin ? 1.f : 0.f;
            continue;
        }
        const float t = std::clamp((s - slopeMin) / span, 0.f, 1.f);
        out[i]        = t * t * (3.f - 2.f * t);
    }
    return publish(cliff, std::move(out));
}

}  // namespace

Result<int> bakeMountainRecipe(Heightmap& target, const MountainRecipeSettings& settings, MountainRecipeMasks masks) {
    if (settings.width < 2 || settings.height < 2 || settings.width > 4096 || settings.height > 4096)
        return invalid("mountain.recipe: width/height must be in [2, 4096]");
    if (!std::isfinite(settings.spacingX) || settings.spacingX <= 0.f || !std::isfinite(settings.spacingZ) ||
        settings.spacingZ <= 0.f || !std::isfinite(settings.heightScale) || !std::isfinite(settings.cliffSlopeMin) ||
        !std::isfinite(settings.cliffSlopeMax) || settings.cliffSlopeMin < 0.f || settings.cliffSlopeMax > 1.f ||
        settings.cliffSlopeMin > settings.cliffSlopeMax)
        return invalid("mountain.recipe: finite positive spacing and cliff slope range in [0,1] required");
    if (settings.preset > MountainPreset::CoastalCliff) return invalid("mountain.recipe: unknown preset");

    PresetNoise noise = noiseFor(settings.preset);
    if (settings.frequencyOverride > 0.f) noise.frequency = settings.frequencyOverride;
    if (settings.ridgeOverride >= 0.f) noise.ridge = settings.ridgeOverride;
    if (settings.warpOverride >= 0.f) noise.warp = settings.warpOverride;
    if (settings.amplitudeOverride >= 0.f) noise.amplitude = settings.amplitudeOverride;

    TerrainSampler sampler;
    sampler.setSeed(settings.seed);
    sampler.setWorldSize(settings.width, settings.height);
    sampler.setFrequency(noise.frequency);
    sampler.setOctaves(noise.octaves);
    sampler.setGain(noise.gain);
    sampler.setRidge(noise.ridge);
    sampler.setWarp(noise.warp);
    sampler.setExponent(noise.exponent);
    sampler.setContinent(noise.continent);
    sampler.setIsland(noise.island);
    sampler.setCoastSoftness(noise.coast);
    sampler.setBase(noise.base);
    sampler.setAmplitude(noise.amplitude);
    sampler.setClamp(true, 0.f, 1.f);

    Heightmap candidate = Heightmap::generate(sampler, settings.width, settings.height);

    PresetErosion erosion = erosionFor(settings.preset);
    if (settings.thermalIterations >= 0) erosion.thermal.iterations = settings.thermalIterations;
    if (settings.hydraulicIterations >= 0) erosion.hydraulic.iterations = settings.hydraulicIterations;
    if (settings.fluvialIterations >= 0) erosion.fluvial.iterations = settings.fluvialIterations;

    TerrainPipeline::erodeThermal(candidate, erosion.thermal);
    TerrainPipeline::erodeHydraulic(candidate, erosion.hydraulic);
    TerrainPipeline::erodeFluvial(candidate, erosion.fluvial);

    // Publish heights first so mask failures do not leave a half-written destination.
    const int changed = settings.width * settings.height;
    target.resize(settings.width, settings.height);
    target.data() = candidate.data();

    if (masks.slopeMask || masks.cliffFitness) {
        Heightmap slope(settings.width, settings.height);
        auto      slopeResult =
            deriveTerrainSlope(slope, target, settings.spacingX, settings.spacingZ, settings.heightScale);
        if (!slopeResult.ok()) return slopeResult;
        if (masks.slopeMask) {
            masks.slopeMask->resize(settings.width, settings.height);
            masks.slopeMask->data() = slope.data();
        }
        if (masks.cliffFitness) {
            masks.cliffFitness->resize(settings.width, settings.height);
            auto cliffResult =
                writeCliffFitness(*masks.cliffFitness, slope, settings.cliffSlopeMin, settings.cliffSlopeMax);
            if (!cliffResult.ok()) return cliffResult;
        }
    }
    return Result<int>::success(changed);
}

}  // namespace eve::procgen
