#include "procgen/heightmap/TerrainMask.h"
#include <array>
#include <cstdint>
#include <numbers>
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
using namespace raster_detail;

Result<int> smoothTerrainMask(Heightmap& target, const Heightmap& input, float verticality, float blurRadius) {
    if (!validRaster(target) || !validRaster(input) || target.getWidth() != input.getWidth() ||
        target.getHeight() != input.getHeight() || !std::isfinite(verticality) || std::abs(verticality) > 1 ||
        !std::isfinite(blurRadius) || blurRadius < 0)
        return invalid("terrain.smoothMask: matching finite rasters, verticality [-1,1] and radius >= 0 required");
    Heightmap candidate = input;
    Heightmap fullMask(input.getWidth(), input.getHeight());
    std::fill(fullMask.data().begin(), fullMask.data().end(), 1.0F);
    TerrainSmoothSettings settings;
    settings.radius      = blurRadius;
    settings.verticality = verticality;
    settings.strength    = 1;
    auto result = applyTerrainSmooth(candidate, fullMask, settings);
    if (!result.ok()) return result;
    return publish(target, std::move(candidate.data()));
}

Result<int> configureTerrainBrushWorld(TerrainBrushBlendSettings& target, const TerrainStampSettings& w,
                                       int contextWidth, int contextHeight, const TerrainSampleGrid& h) {
    auto positive = [](double v) { return std::isfinite(v) && v > 0; };
    if (contextWidth <= 0 || contextHeight <= 0 || h.width <= 0 || h.height <= 0 || !positive(w.spacingX) ||
        !positive(w.spacingZ) || !positive(h.spacingX) || !positive(h.spacingZ) || !positive(w.width) ||
        !positive(w.depth) || !std::isfinite(target.strength))
        return invalid("terrain.paintContext: positive dimensions, spacing, footprint and finite strength required");
    for (double v : {w.originX, w.originZ, w.centerX, w.centerZ, w.rotation, h.originX, h.originZ})
        if (!std::isfinite(v)) return invalid("terrain.paintContext: finite world coordinates required");
    const double spanX = contextWidth * w.spacingX, spanZ = contextHeight * w.spacingZ;
    const double heightSpanX = h.width * h.spacingX, heightSpanZ = h.height * h.spacingZ;
    if (!std::isfinite(spanX) || !std::isfinite(spanZ) || !std::isfinite(heightSpanX) || !std::isfinite(heightSpanZ))
        return invalid("terrain.paintContext: world spans exceed finite double range");
    const double c = std::cos(w.rotation), s = std::sin(w.rotation);
    const double bx = (w.originX - w.centerX) - 0.5 * w.spacingX, bz = (w.originZ - w.centerZ) - 0.5 * w.spacingZ;
    const double values[] = {spanX / heightSpanX,
                             0,
                             0,
                             spanZ / heightSpanZ,
                             ((w.originX - h.originX) - 0.5 * w.spacingX + 0.5 * h.spacingX) / heightSpanX,
                             ((w.originZ - h.originZ) - 0.5 * w.spacingZ + 0.5 * h.spacingZ) / heightSpanZ,
                             c * spanX / w.width,
                             s * spanZ / w.width,
                             -s * spanX / w.depth,
                             c * spanZ / w.depth,
                             0.5 + (c * bx + s * bz) / w.width,
                             0.5 + (-s * bx + c * bz) / w.depth};
    if (!std::all_of(std::begin(values), std::end(values), [](double v) { return isRepresentable(v); }))
        return invalid("terrain.paintContext: UV coefficients exceed finite float range");
    TerrainBrushBlendSettings candidate = target;
    float*                    fields[]  = {&candidate.heightXX, &candidate.heightXZ,      &candidate.heightZX,
                                           &candidate.heightZZ, &candidate.heightOffsetX, &candidate.heightOffsetZ,
                                           &candidate.brushXX,  &candidate.brushXZ,       &candidate.brushZX,
                                           &candidate.brushZZ,  &candidate.brushOffsetX,  &candidate.brushOffsetZ};
    int                       changed   = 0;
    for (size_t i = 0; i < std::size(values); ++i) {
        changed += *fields[i] != float(values[i]);
        *fields[i] = float(values[i]);
    }
    target = candidate;
    return Result<int>::success(changed);
}


Result<int> generateTerrainConcavityMask(Heightmap& target, const Heightmap& input, const Heightmap& heights,
                                         const Heightmap& curve, const TerrainConcavitySettings& s,
                                         TerrainMaskBlend mode) {
    if (!validRaster(target) || !validRaster(input) || !validRaster(heights) || !validRaster(curve) ||
        target.getWidth() != input.getWidth() || target.getHeight() != input.getHeight() || curve.getHeight() != 1 ||
        !std::isfinite(s.featureSize) || s.featureSize <= 0 || double(s.featureSize) > UINT32_MAX ||
        !std::isfinite(s.concavity) || mode < TerrainMaskBlend::Multiply || mode > TerrainMaskBlend::Subtract)
        return invalid("terrain.concavity: finite compatible rasters and valid feature controls required");
    const double factor = double(heights.getWidth()) / input.getWidth();
    if ((input.getHeight() - 1) * factor > UINT32_MAX)
        return invalid("terrain.concavity: mapped coordinates exceed uint32");
    const uint32_t width = heights.getWidth(), height = heights.getHeight(), offset = uint32_t(s.featureSize);
    auto           sample = [&](uint32_t x, uint32_t z) -> double {
        return x < width && z < height ? heights.height(int(x), int(z)) : 0;
    };
    auto gradient = [&](uint32_t x, uint32_t z) {
        const uint32_t r = x - offset, l = std::min(width, uint32_t(x + offset));
        const uint32_t u = z - offset, d = std::min(height, uint32_t(z + offset));
        const double   dx =
            (sample(r, u) + 2 * sample(r, z) + sample(r, d) - sample(l, u) - 2 * sample(l, z) - sample(l, d)) / 8;
        const double dz =
            (sample(l, d) + 2 * sample(x, d) + sample(r, d) - sample(l, u) - 2 * sample(x, u) - sample(r, u)) / 8;
        const double length = std::hypot(dx, dz);
        return length == 0 ? std::array<double, 2>{0, 0} : std::array<double, 2>{dx / length, dz / length};
    };
    std::vector<float> output(target.data().size());
    for (int z = 0; z < input.getHeight(); ++z)
        for (int x = 0; x < input.getWidth(); ++x) {
            const uint32_t hx = uint32_t(x * factor), hz = uint32_t(z * factor);
            const uint32_t r = hx - offset, l = std::min(width, uint32_t(hx + offset));
            const uint32_t u = hz - offset, d = std::min(height, uint32_t(hz + offset));
            const auto     ru = gradient(r, u), rc = gradient(r, hz), rd = gradient(r, d);
            const auto     lu = gradient(l, u), lc = gradient(l, hz), ld = gradient(l, d);
            const auto     cd = gradient(hx, d), cu = gradient(hx, u);
            const double   dx     = (ru[0] + 2 * rc[0] + rd[0] - lu[0] - 2 * lc[0] - ld[0]) / 8;
            const double   dz     = (ld[1] + 2 * cd[1] + rd[1] - lu[1] - 2 * cu[1] - ru[1]) / 8;
            const uint32_t border = std::min({uint32_t(width - hx), hx, uint32_t(height - hz), hz});
            double         fade   = std::clamp((double(border) - 2 * s.featureSize) / (2 * s.featureSize), 0.0, 1.0);
            fade                  = fade * fade * (3 - 2 * fade);
            const double value =
                std::clamp(2 * fade * std::clamp(double(s.concavity) * (dx + dz) / 2, 0.0, 1.0), 0.0, 1.0);
            const double filter = curve.height(int(value * (curve.getWidth() - 1)), 0);
            const size_t i      = size_t(z) * input.getWidth() + x;
            const double old    = input.data()[i];
            double       result = old;
            switch (mode) {
                case TerrainMaskBlend::Multiply: result = old * filter; break;
                case TerrainMaskBlend::Maximum: result = std::max(old, filter); break;
                case TerrainMaskBlend::Minimum: result = std::min(old, filter); break;
                case TerrainMaskBlend::Add: result = old + filter; break;
                case TerrainMaskBlend::Subtract: result = old - filter; break;
            }
            if (!isRepresentable(result)) return invalid("terrain.concavity: result exceeds finite float range");
            output[i] = float(result);
        }
    return publish(target, std::move(output));
}

// Radial blur design attribution from the reference shader:
// MIT License, Copyright (c) 2018 @XorDev
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
Result<int> generateTerrainCurvatureMask(Heightmap& target, const Heightmap& input, const Heightmap& heights,
                                         const Heightmap& curve, const TerrainCurvatureSettings& s,
                                         TerrainMaskBlend mode) {
    if (!validRaster(target) || !validRaster(input) || !validRaster(heights) || !validRaster(curve) ||
        curve.getHeight() != 1 || target.getWidth() != input.getWidth() || target.getHeight() != input.getHeight() ||
        !std::isfinite(s.radius) || s.radius < 0 || !std::isfinite(s.worldUnits) || !std::isfinite(s.intensity) ||
        s.intensity <= 0 || s.steps <= 0 || s.directions <= 0 ||
        s.steps > (std::numeric_limits<int>::max() - 1) / s.directions || mode < TerrainMaskBlend::Multiply ||
        mode > TerrainMaskBlend::Subtract)
        return invalid("terrain.curvature: finite compatible rasters, curve and valid radial controls required");
    std::vector<float> output(target.data().size());
    for (int z = 0; z < target.getHeight(); ++z)
        for (int x = 0; x < target.getWidth(); ++x) {
            const double u = (x + 0.5) / target.getWidth(), v = (z + 0.5) / target.getHeight();
            const double origin = textureSample(heights, u, v);
            double       sum    = origin;
            for (int direction = 0; direction < s.directions; ++direction) {
                const double angle = 2 * std::numbers::pi * direction / s.directions;
                for (int step = 1; step <= s.steps; ++step) {
                    const double radius = double(s.radius) * step / s.steps;
                    sum += textureSample(heights, u + std::cos(angle) * radius, v + std::sin(angle) * radius);
                }
            }
            const double base = (origin - sum / (double(s.steps) * s.directions + 1)) * s.worldUnits;
            if (base < 0 && std::floor(s.intensity) != s.intensity)
                return invalid("terrain.curvature: negative base with fractional intensity has no real result");
            const double powered = std::pow(base, double(s.intensity));
            if (std::isnan(powered)) return invalid("terrain.curvature: undefined power result");
            const double filter   = curveSample(curve, std::clamp(std::abs(powered), 0.0, 1.0));
            const size_t i        = size_t(z) * target.getWidth() + x;
            const double previous = input.data()[i];
            double       result   = previous;
            switch (mode) {
                case TerrainMaskBlend::Multiply: result = previous * filter; break;
                case TerrainMaskBlend::Maximum: result = std::max(previous, filter); break;
                case TerrainMaskBlend::Minimum: result = filter < 1 - previous ? filter : previous; break;
                case TerrainMaskBlend::Add: result = previous + filter; break;
                case TerrainMaskBlend::Subtract: result = previous - filter; break;
            }
            if (!isRepresentable(result)) return invalid("terrain.curvature: result exceeds finite float range");
            output[i] = float(result);
        }
    return publish(target, std::move(output));
}

Result<int> generateTerrainErosionMask(Heightmap& target, const Heightmap& oldHeights, const Heightmap& erosion,
                                       const Heightmap& brush, const TerrainBrushBlendSettings& spatial,
                                       const Heightmap& curve, TerrainStrengthMode mode, float strength,
                                       bool userInvert) {
    Heightmap candidate = target;
    auto      blended   = blendTerrainBrush(candidate, oldHeights, erosion, brush, spatial);
    if (!blended.ok()) return blended;
    auto filtered = applyTerrainStrength(candidate, candidate, curve, mode, strength, !userInvert);
    if (!filtered.ok()) return filtered;
    return publish(target, std::move(candidate.data()));
}
Result<int> growShrinkTerrainMask(Heightmap& target, const Heightmap& source, const Heightmap& curve, float distance) {
    const double radius = std::abs(double(distance));
    if (!validRaster(target) || !validRaster(source) || !validRaster(curve) || curve.getHeight() != 1 ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight() ||
        !std::isfinite(distance) || 2 * radius * source.getWidth() > std::numeric_limits<int>::max())
        return invalid(
            "terrain.growShrink: matching finite rasters, one-row curve and indexable finite radius required");
    const int          width = source.getWidth(), height = source.getHeight();
    const long long    last = static_cast<long long>(std::floor(2 * radius * width));
    std::vector<float> output(target.data().size());
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            const double u = (x + 0.5) / width, v = (z + 0.5) / height;
            double       value = source.height(x, z);
            if (radius > 0) {
                // Skip definitely out-of-bounds indices, retaining one boundary candidate for rounding.
                const auto firstX = std::max(0LL, static_cast<long long>(std::floor((radius - u) * width)) - 1);
                const auto lastX  = std::min(last, static_cast<long long>(std::ceil((radius + 1 - u) * width)) + 1);
                const auto firstZ = std::max(0LL, static_cast<long long>(std::floor((radius - v) * width)) - 1);
                const auto lastZ  = std::min(last, static_cast<long long>(std::ceil((radius + 1 - v) * width)) + 1);
                for (auto ix = firstX; ix <= lastX; ++ix)
                    for (auto iz = firstZ; iz <= lastZ; ++iz) {
                        const double dx = -radius + double(ix) / width, dz = -radius + double(iz) / width;
                        if (u + dx < 0 || u + dx > 1 || v + dz < 0 || v + dz > 1) continue;
                        double weight          = std::clamp(std::hypot(dx, dz) / std::hypot(radius, radius), 0.0, 1.0);
                        weight                 = weight * weight * (3 - 2 * weight);
                        const double candidate = std::lerp(textureSample(source, u + dx, v + dz), value, weight);
                        value                  = distance > 0 ? std::max(value, candidate) : std::min(value, candidate);
                    }
            }
            const double transformed = curveSample(curve, value);
            if (!isRepresentable(transformed)) return invalid("terrain.growShrink: result exceeds finite float range");
            output[size_t(z) * width + x] = float(transformed);
        }
    return publish(target, std::move(output));
}
Result<int> applyTerrainStrength(Heightmap& target, const Heightmap& source, const Heightmap& curve,
                                 TerrainStrengthMode mode, float strength, bool invert) {
    if (!validRaster(target) || !validRaster(source) || !validRaster(curve) || curve.getHeight() != 1 ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight() ||
        !std::isfinite(strength) || strength < 0 || strength > 1 || mode < TerrainStrengthMode::Replace ||
        mode > TerrainStrengthMode::Subtract)
        return invalid(
            "terrain.strength: matching finite rasters, one-row curve, valid mode and unit strength required");
    std::vector<float> output(target.data().size());
    for (size_t i = 0; i < output.size(); ++i) {
        const double old   = source.data()[i];
        double       value = curveSample(curve, old);
        if (invert) value = 1 - value;
        switch (mode) {
            case TerrainStrengthMode::Replace: break;
            case TerrainStrengthMode::Maximum: value = std::max(old, value); break;
            case TerrainStrengthMode::Minimum: value = std::min(old, value); break;
            case TerrainStrengthMode::Add: value = old + value; break;
            case TerrainStrengthMode::Subtract: value = old - value; break;
        }
        value = std::lerp(old, value, double(strength));
        if (!isRepresentable(value)) return invalid("terrain.strength: output exceeds finite float range");
        output[i] = float(value);
    }
    return publish(target, std::move(output));
}
Result<int> blendTerrainBrush(Heightmap& target, const Heightmap& oldHeights, const Heightmap& newHeights,
                              const Heightmap& brush, const TerrainBrushBlendSettings& s) {
    const float coefficients[] = {s.heightXX,      s.heightXZ,     s.heightZX, s.heightZZ, s.heightOffsetX,
                                  s.heightOffsetZ, s.brushXX,      s.brushXZ,  s.brushZX,  s.brushZZ,
                                  s.brushOffsetX,  s.brushOffsetZ, s.strength};
    if (!validRaster(target) || !validRaster(oldHeights) || !validRaster(newHeights) || !validRaster(brush) ||
        !std::all_of(std::begin(coefficients), std::end(coefficients), [](float v) { return std::isfinite(v); }))
        return invalid("terrain.brush: finite rasters and transform coefficients required");
    std::vector<float> output(target.data().size());
    for (int z = 0; z < target.getHeight(); ++z)
        for (int x = 0; x < target.getWidth(); ++x) {
            const double u = (x + 0.5) / target.getWidth(), v = (z + 0.5) / target.getHeight();
            const double hu = s.heightXX * u + s.heightXZ * v + s.heightOffsetX,
                         hv = s.heightZX * u + s.heightZZ * v + s.heightOffsetZ;
            const double bu = s.brushXX * u + s.brushXZ * v + s.brushOffsetX,
                         bv = s.brushZX * u + s.brushZZ * v + s.brushOffsetZ;
            double result   = textureSample(oldHeights, hu, hv);
            if (bu >= 0 && bu <= 1 && bv >= 0 && bv <= 1)
                result = std::lerp(result, textureSample(newHeights, hu, hv),
                                   double(s.strength) * textureSample(brush, bu, bv));
            if (!isRepresentable(result)) return invalid("terrain.brush: result exceeds finite float range");
            output[size_t(z) * target.getWidth() + x] = float(result);
        }
    return publish(target, std::move(output));
}
namespace {
bool sameShape(const Heightmap& a, const Heightmap& b) {
    return validRaster(a) && validRaster(b) && a.getWidth() == b.getWidth() && a.getHeight() == b.getHeight();
}
bool   validCurve(const Heightmap& curve) { return validRaster(curve) && curve.getHeight() == 1; }
double smooth(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value * value * (3 - 2 * value);
}
double wrap(double value) {
    // Match the source shader's boundary convention: zero/negative integers become 1.
    return value > 0 ? std::fmod(value, 1.0) : 1.0 + std::fmod(value, 1.0);
}
}  // namespace

Result<int> transformTerrainMask(Heightmap& target, const Heightmap& source, const Heightmap& curve) {
    if (!sameShape(target, source) || !validCurve(curve))
        return invalid("terrain.mask.transform: matching finite rasters and a one-row curve required");
    std::vector<float> result(target.data().size());
    for (size_t i = 0; i < result.size(); ++i) result[i] = float(curveSample(curve, source.data()[i]));
    return publish(target, std::move(result));
}

Result<int> generateTerrainRangeMask(Heightmap& target, const Heightmap& source, float minimum, float maximum,
                                     const Heightmap& filterCurve, const Heightmap& strengthCurve) {
    if (!sameShape(target, source) || !validCurve(filterCurve) || !validCurve(strengthCurve) ||
        !std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum)
        return invalid(
            "terrain.mask.range: matching finite rasters, one-row curves and increasing finite range required");
    std::vector<float> result(target.data().size());
    for (size_t i = 0; i < result.size(); ++i) {
        const double t = smooth((double(source.data()[i]) - minimum) / (double(maximum) - minimum));
        result[i]      = float(curveSample(strengthCurve, curveSample(filterCurve, t)));
    }
    return publish(target, std::move(result));
}

Result<int> deriveTerrainSlope(Heightmap& target, const Heightmap& heights, float spacingX, float spacingZ,
                               float heightScale) {
    if (!sameShape(target, heights) || !std::isfinite(spacingX) || !std::isfinite(spacingZ) ||
        !std::isfinite(heightScale) || spacingX <= 0 || spacingZ <= 0)
        return invalid(
            "terrain.mask.slope: matching finite rasters, positive spacing and finite height scale required");
    std::vector<float> result(target.data().size());
    for (int y = 0; y < heights.getHeight(); ++y) {
        for (int x = 0; x < heights.getWidth(); ++x) {
            const int left = std::max(0, x - 1), right = std::min(x + 1, heights.getWidth() - 1);
            const int top = std::max(0, y - 1), bottom = std::min(y + 1, heights.getHeight() - 1);
            double    dx = 0, dz = 0;
            if (right > left)
                dx = (double(heights.height(right, y)) - heights.height(left, y)) * heightScale /
                     (double(right - left) * spacingX);
            if (bottom > top)
                dz = (double(heights.height(x, bottom)) - heights.height(x, top)) * heightScale /
                     (double(bottom - top) * spacingZ);
            result[size_t(y) * heights.getWidth() + x] = float(1 - 1 / std::hypot(1.0, dx, dz));
        }
    }
    return publish(target, std::move(result));
}

Result<int> generateTerrainDistanceMask(Heightmap& target, const TerrainDistanceMaskSettings& s,
                                        const Heightmap& filterCurve, const Heightmap& strengthCurve) {
    if (!validRaster(target) || !validCurve(filterCurve) || !validCurve(strengthCurve) ||
        s.axis < TerrainDistanceAxis::Circle || s.axis > TerrainDistanceAxis::RoundedSquare || s.scaleX == 0 ||
        s.scaleZ == 0 || s.roundness <= 0)
        return invalid("terrain.mask.distance: valid raster, curves, axis, scales and roundness required");
    for (float value : {s.offsetX, s.offsetZ, s.scaleX, s.scaleZ, s.rotation, s.roundness})
        if (!std::isfinite(value)) return invalid("terrain.mask.distance: finite settings required");
    const double       c = std::cos(double(s.rotation)), sn = std::sin(double(s.rotation));
    std::vector<float> result(target.data().size());
    for (int y = 0; y < target.getHeight(); ++y) {
        for (int x = 0; x < target.getWidth(); ++x) {
            const double px = ((x + 0.5) / target.getWidth() - 0.5 - s.offsetX) / s.scaleX;
            const double pz = ((y + 0.5) / target.getHeight() - 0.5 - s.offsetZ) / s.scaleZ;
            double       u = c * px + sn * pz + 0.5, v = -sn * px + c * pz + 0.5;
            if (s.tiling) {
                u = wrap(u);
                v = wrap(v);
            }
            double parameter = 0, filtered = 0;
            if (s.axis != TerrainDistanceAxis::RoundedSquare || (u >= 0 && u <= 1 && v >= 0 && v <= 1)) {
                switch (s.axis) {
                    case TerrainDistanceAxis::Circle: parameter = smooth(2 * std::hypot(u - 0.5, v - 0.5)); break;
                    case TerrainDistanceAxis::X: parameter = u; break;
                    case TerrainDistanceAxis::Z: parameter = v; break;
                    case TerrainDistanceAxis::RoundedSquare:
                        parameter = 1 - 2 * std::pow(u * (1 - v) * v * (1 - u), double(s.roundness));
                        break;
                }
                filtered = curveSample(filterCurve, parameter);
            }
            result[size_t(y) * target.getWidth() + x] = float(curveSample(strengthCurve, filtered));
        }
    }
    return publish(target, std::move(result));
}
}  // namespace eve::procgen
