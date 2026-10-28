#include "procgen/algorithms/CaveDepositAnchoring.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float lengthSquared(CaveFieldPoint point) { return point.x * point.x + point.y * point.y + point.z * point.z; }

CaveFieldPoint normalize(CaveFieldPoint point) {
    const float length2 = lengthSquared(point);
    if (length2 < 1e-10f) return {};
    const float inverseLength = 1.f / std::sqrt(length2);
    return {point.x * inverseLength, point.y * inverseLength, point.z * inverseLength};
}

float interpolateZero(float aPosition, float aValue, float bPosition, float bValue) {
    const float denominator = aValue - bValue;
    if (std::abs(denominator) < 1e-8f) return (aPosition + bPosition) * 0.5f;
    return aPosition + (bPosition - aPosition) * std::clamp(aValue / denominator, 0.f, 1.f);
}

CaveSurfaceAnchor makeAnchor(const std::vector<float>& density, int nx, int ny, int nz, CaveFieldPoint position) {
    return {position, normalize(sampleCaveDensityGradient(density, nx, ny, nz, position))};
}

}  // namespace

std::optional<CaveVerticalSpan> findCaveVerticalSpan(const std::vector<float>& density, int nx, int ny, int nz, float x,
                                                     float z, float preferredY) {
    if (nx < 2 || ny < 3 || nz < 2 || density.size() != size_t(nx) * size_t(ny) * size_t(nz)) return std::nullopt;
    x          = std::clamp(x, -1.f, 1.f);
    z          = std::clamp(z, -1.f, 1.f);
    preferredY = std::clamp(preferredY, -1.f, 1.f);

    struct Interval {
        float floorY;
        float ceilingY;
    };
    std::vector<Interval> intervals;
    bool                  inAir       = false;
    float                 floorY      = -1.f;
    float                 previousY   = -1.f;
    float                 previousVal = sampleCaveDensity(density, nx, ny, nz, {x, previousY, z});
    for (int yIndex = 1; yIndex < ny; ++yIndex) {
        const float y     = float(yIndex) / float(ny - 1) * 2.f - 1.f;
        const float value = sampleCaveDensity(density, nx, ny, nz, {x, y, z});
        if (!inAir && previousVal >= 0.f && value < 0.f) {
            floorY = interpolateZero(previousY, previousVal, y, value);
            inAir  = true;
        } else if (inAir && previousVal < 0.f && value >= 0.f) {
            intervals.push_back({floorY, interpolateZero(previousY, previousVal, y, value)});
            inAir = false;
        }
        previousY   = y;
        previousVal = value;
    }
    if (intervals.empty()) return std::nullopt;

    const Interval* selected = nullptr;
    for (const Interval& interval : intervals) {
        if (preferredY >= interval.floorY && preferredY <= interval.ceilingY) {
            selected = &interval;
            break;
        }
    }
    if (selected == nullptr) {
        selected = &*std::min_element(intervals.begin(), intervals.end(), [preferredY](const auto& a, const auto& b) {
            const float aDistance = std::abs((a.floorY + a.ceilingY) * 0.5f - preferredY);
            const float bDistance = std::abs((b.floorY + b.ceilingY) * 0.5f - preferredY);
            return aDistance < bDistance;
        });
    }
    const float minimumGap = 4.f / float(ny - 1);
    if (selected->ceilingY - selected->floorY < minimumGap) return std::nullopt;

    CaveVerticalSpan span;
    span.floor   = makeAnchor(density, nx, ny, nz, {x, selected->floorY, z});
    span.ceiling = makeAnchor(density, nx, ny, nz, {x, selected->ceilingY, z});
    if (lengthSquared(span.floor.rockNormal) < 0.5f || lengthSquared(span.ceiling.rockNormal) < 0.5f)
        return std::nullopt;
    return span;
}

std::optional<CaveSurfaceAnchor> projectToFinalCaveSurface(const std::vector<float>& density, int nx, int ny, int nz,
                                                           CaveFieldPoint point, float maximumDistance) {
    if (nx < 2 || ny < 2 || nz < 2 || density.size() != size_t(nx) * size_t(ny) * size_t(nz) || maximumDistance <= 0.f)
        return std::nullopt;
    const CaveFieldPoint original = point;
    for (int iteration = 0; iteration < 10; ++iteration) {
        const float          value           = sampleCaveDensity(density, nx, ny, nz, point);
        const CaveFieldPoint gradient        = sampleCaveDensityGradient(density, nx, ny, nz, point);
        const float          gradientLength2 = lengthSquared(gradient);
        if (gradientLength2 < 1e-8f) return std::nullopt;
        point.x = std::clamp(point.x - gradient.x * value / gradientLength2, -1.f, 1.f);
        point.y = std::clamp(point.y - gradient.y * value / gradientLength2, -1.f, 1.f);
        point.z = std::clamp(point.z - gradient.z * value / gradientLength2, -1.f, 1.f);
    }
    const CaveFieldPoint displacement{point.x - original.x, point.y - original.y, point.z - original.z};
    const float          tolerance = 1.f / float(std::min({nx - 1, ny - 1, nz - 1}));
    if (lengthSquared(displacement) > maximumDistance * maximumDistance ||
        std::abs(sampleCaveDensity(density, nx, ny, nz, point)) > tolerance)
        return std::nullopt;
    CaveSurfaceAnchor anchor = makeAnchor(density, nx, ny, nz, point);
    if (lengthSquared(anchor.rockNormal) < 0.5f) return std::nullopt;
    return anchor;
}

}  // namespace eve::procgen
