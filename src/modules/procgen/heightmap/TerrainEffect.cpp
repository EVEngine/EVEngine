#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

namespace eve::procgen {
namespace {
using namespace raster_detail;
bool isUnit(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool validEffectInputs(const Heightmap& target, const Heightmap& mask) {
    return validRaster(target) && validRaster(mask) && target.getWidth() == mask.getWidth() &&
           target.getHeight() == mask.getHeight() && std::all_of(mask.data().begin(), mask.data().end(), isUnit);
}
double pixelSample(const Heightmap& map, double x, double y) {
    const double width = map.getWidth() - 1, height = map.getHeight() - 1;
    return sample(map, width == 0 ? 0 : std::clamp(x / width, 0.0, 1.0),
                  height == 0 ? 0 : std::clamp(y / height, 0.0, 1.0));
}
double roundEven(double value) {
    // HLSL round uses nearest-even, independently of the host floating-point rounding mode.
    const double lower = std::floor(value), fraction = value - lower;
    if (fraction < 0.5) return lower;
    if (fraction > 0.5) return lower + 1;
    return std::fmod(lower, 2.0) == 0 ? lower : lower + 1;
}
}  // namespace

Result<int> applyTerrainContrast(Heightmap& target, const Heightmap& mask, float strength, float featureSize) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !std::isfinite(strength) || strength < 0 || !std::isfinite(featureSize) ||
        featureSize < 0)
        return invalid("terrain.contrast: finite matching rasters, unit mask and nonnegative strength/size required");
    auto               tap = [&](double x, double y) { return pixelSample(target, x, y); };
    std::vector<float> result(target.data().size());
    for (int y = 0; y < target.getHeight(); ++y) {
        for (int x = 0; x < target.getWidth(); ++x) {
            const size_t i        = size_t(y) * target.getWidth() + x;
            const double original = target.data()[i], d = featureSize;
            const double average =
                (original + tap(x - d, y) + tap(x + d, y) + tap(x, y - d) + tap(x, y + d) +
                 0.75 * (tap(x - d, y - d) + tap(x + d, y - d) + tap(x - d, y + d) + tap(x + d, y + d))) /
                8;
            const double value = original + (original - average) * 0.5 * strength * mask.data()[i];
            if (!isRepresentable(value)) return invalid("terrain.contrast: output exceeds finite float range");
            result[i] = float(value);
        }
    }
    return publish(target, std::move(result));
}

Result<int> applyTerrainSmooth(Heightmap& target, const Heightmap& mask, const TerrainSmoothSettings& settings) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !isUnit(settings.strength) || !std::isfinite(settings.radius) ||
        settings.radius < 0 || !std::isfinite(settings.verticality) || std::abs(settings.verticality) > 1)
        return invalid(
            "terrain.smooth: matching unit mask, unit strength, nonnegative radius and verticality [-1,1] required");
    constexpr double   weights[] = {0.95, 0.85, 0.7, 0.4, 0.2, 0.15, 0.05};
    Heightmap          current   = target;
    std::vector<float> result(target.data().size());
    const double       centered = 1 - std::abs(double(settings.verticality));
    const double       lower    = std::max(-double(settings.verticality), 0.0);
    const double       upper    = std::max(double(settings.verticality), 0.0);
    for (int pass = 0; pass < 2; ++pass) {
        // Source shader advances vertical UV by texelSize.x as well.
        const double radius =
            pass == 0 ? settings.radius : double(settings.radius) * current.getHeight() / current.getWidth();
        for (int y = 0; y < current.getHeight(); ++y) {
            for (int x = 0; x < current.getWidth(); ++x) {
                const size_t index    = size_t(y) * current.getWidth() + x;
                const double original = current.data()[index];
                double       average  = original;
                for (int k = 0; k < 7; ++k) {
                    const double offset = radius * (k + 1);
                    const double dx = pass == 0 ? offset : 0, dy = pass == 0 ? 0 : offset;
                    average +=
                        weights[k] * (pixelSample(current, x - dx, y - dy) + pixelSample(current, x + dx, y + dy));
                }
                average /= 7.6;
                const double filtered =
                    centered * average + lower * std::min(average, original) + upper * std::max(average, original);
                const double value = std::lerp(original, filtered, double(settings.strength) * mask.data()[index]);
                if (!isRepresentable(value)) return invalid("terrain.smooth: output exceeds finite float range");
                result[index] = float(value);
            }
        }
        current.data().swap(result);
    }
    return publish(target, std::move(current.data()));
}

Result<int> applyTerrainRidges(Heightmap& target, const Heightmap& mask, const TerrainRidgeSettings& settings) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !isUnit(settings.strength) || !isUnit(settings.mixStrength) ||
        !std::isfinite(settings.exponent) || settings.exponent <= 0 || !std::isfinite(settings.minimum) ||
        !std::isfinite(settings.maximum) || settings.minimum >= settings.maximum || settings.passes < 0)
        return invalid(
            "terrain.ridges: finite matching unit mask, unit strengths, positive exponent, ordered bounds and "
            "nonnegative passes required");
    Heightmap          current = target;
    std::vector<float> result(target.data().size());
    auto               powerBetween = [&](double h, double a, double b) {
        const double low = std::min(a, b), high = std::max(a, b);
        return h > low && h < high ? std::pow((h - low) / (high - low), settings.exponent) * (high - low) + low : h;
    };
    for (int pass = 0; pass < settings.passes; ++pass) {
        for (int y = 0; y < current.getHeight(); ++y) {
            for (int x = 0; x < current.getWidth(); ++x) {
                const size_t index    = size_t(y) * current.getWidth() + x;
                const double original = current.data()[index];
                const double left     = current.height(std::max(x - 1, 0), y);
                const double right    = current.height(std::min(x + 1, current.getWidth() - 1), y);
                const double top      = current.height(x, std::max(y - 1, 0));
                const double bottom   = current.height(x, std::min(y + 1, current.getHeight() - 1));
                double       value    = powerBetween(original, left, right);
                value                 = powerBetween(value, bottom, top);
                value                 = std::lerp(0.25 * (left + right + top + bottom), value, settings.mixStrength);
                value         = std::clamp(std::lerp(original, value, double(settings.strength) * mask.data()[index]),
                                           double(settings.minimum), double(settings.maximum));
                result[index] = float(value);
            }
        }
        current.data().swap(result);
    }
    return publish(target, std::move(current.data()));
}

Result<int> applyTerrainTerrace(Heightmap& target, const Heightmap& mask, const TerrainTerraceSettings& settings) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !isUnit(settings.strength) || !isUnit(settings.bevel) ||
        !std::isfinite(settings.count) || settings.count <= 0)
        return invalid("terrain.terrace: finite matching unit mask, unit strength/bevel and positive count required");
    std::vector<float> result(target.data().size());
    for (size_t i = 0; i < result.size(); ++i) {
        const double original = target.data()[i], scaled = original * settings.count;
        double       rounded = roundEven(scaled);
        if (scaled - rounded > 1 - double(settings.bevel)) rounded = scaled;
        const double value = std::lerp(original, rounded / settings.count, double(settings.strength) * mask.data()[i]);
        if (!isRepresentable(value)) return invalid("terrain.terrace: output exceeds finite float range");
        result[i] = float(value);
    }
    return publish(target, std::move(result));
}

Result<int> applyTerrainPower(Heightmap& target, const Heightmap& mask, float power) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !std::isfinite(power))
        return invalid("terrain.power: finite matching unit mask and finite power required");
    const double       exponent = 4 - double(power);
    std::vector<float> result   = target.data();
    for (size_t i = 0; i < result.size(); ++i) {
        if (mask.data()[i] == 0) continue;
        const double original = target.data()[i];
        if (original < 0 || (original == 0 && exponent <= 0))
            return invalid("terrain.power: negative base or zero base with nonpositive exponent");
        const double value = std::lerp(original, std::pow(original, exponent), double(mask.data()[i]));
        if (!isRepresentable(value)) return invalid("terrain.power: output exceeds finite float range");
        result[i] = float(value);
    }
    return publish(target, std::move(result));
}

Result<int> applyTerrainHeightCurve(Heightmap& target, const Heightmap& mask, const Heightmap& curve, float minimum,
                                    float maximum) {
    using namespace raster_detail;
    if (!validEffectInputs(target, mask) || !validRaster(curve) || curve.getHeight() != 1 || !std::isfinite(minimum) ||
        !std::isfinite(maximum) || minimum >= maximum)
        return invalid("terrain.heightCurve: matching unit mask, one-row curve and ordered finite range required");
    std::vector<float> result = target.data();
    for (size_t i = 0; i < result.size(); ++i) {
        if (mask.data()[i] == 0) continue;
        const double original = target.data()[i], range = double(maximum) - minimum;
        double       t     = std::clamp((original - minimum) / range, 0.0, 1.0);
        t                  = t * t * (3 - 2 * t);
        const double value = std::lerp(original, range * curveSample(curve, t), double(mask.data()[i]));
        if (!isRepresentable(value)) return invalid("terrain.heightCurve: output exceeds finite float range");
        result[i] = float(value);
    }
    return publish(target, std::move(result));
}

Result<int> applyTerrainHeightMix(Heightmap& target, const Heightmap& local, const Heightmap& global,
                                  const TerrainHeightMixSettings& settings) {
    using namespace raster_detail;
    if (!validEffectInputs(target, global) || !validRaster(local) || local.getWidth() != target.getWidth() ||
        local.getHeight() != target.getHeight() || !std::isfinite(settings.minimum) ||
        !std::isfinite(settings.maximum) || settings.minimum > settings.maximum || !isUnit(settings.midpoint) ||
        !std::isfinite(settings.strength) || settings.strength < 0 || !std::isfinite(settings.clipMinimum) ||
        !std::isfinite(settings.clipMaximum) || settings.clipMinimum >= settings.clipMaximum)
        return invalid(
            "terrain.heightMix: finite matching rasters, unit global/midpoint, nonnegative strength and ordered ranges "
            "required");
    std::vector<float> result(target.data().size());
    for (size_t i = 0; i < result.size(); ++i) {
        const double delta = (double(local.data()[i]) - settings.midpoint) *
                             (double(settings.maximum) - settings.minimum) * settings.strength;
        const double value = double(target.data()[i]) + delta * global.data()[i];
        result[i]          = float(std::clamp(value, double(settings.clipMinimum), double(settings.clipMaximum)));
    }
    return publish(target, std::move(result));
}
}  // namespace eve::procgen
