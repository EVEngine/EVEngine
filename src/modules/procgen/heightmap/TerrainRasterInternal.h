#pragma once

#include "common/Result.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

// Shared implementation details for the stamp and mask kernels.
namespace eve::procgen::raster_detail {
inline bool validRaster(const Heightmap& map) {
    return map.getWidth() > 0 && map.getHeight() > 0 &&
           map.getWidth() <= std::numeric_limits<int>::max() / map.getHeight() &&
           map.data().size() == size_t(map.getWidth()) * size_t(map.getHeight()) &&
           map.data().size() <= size_t(std::numeric_limits<int>::max()) &&
           std::all_of(map.data().begin(), map.data().end(), [](float v) { return std::isfinite(v); });
}

inline Result<int> invalid(std::string message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message)));
}

inline bool isRepresentable(double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}

// Clamp/Bilinear texture-center sampling. Callers validate finite coordinates and rasters.
inline double textureSample(const Heightmap& map, double u, double v) {
    const double x  = std::clamp(u * map.getWidth() - 0.5, 0.0, double(map.getWidth() - 1));
    const double z  = std::clamp(v * map.getHeight() - 0.5, 0.0, double(map.getHeight() - 1));
    const int    x0 = int(x), z0 = int(z), x1 = std::min(x0 + 1, map.getWidth() - 1),
              z1 = std::min(z0 + 1, map.getHeight() - 1);
    return std::lerp(std::lerp(double(map.height(x0, z0)), double(map.height(x1, z0)), x - x0),
                     std::lerp(double(map.height(x0, z1)), double(map.height(x1, z1)), x - x0), z - z0);
}

// Double intermediates avoid overflow when interpolating large finite floats.
inline double sample(const Heightmap& map, double u, double v) {
    const double x = u * (map.getWidth() - 1), y = v * (map.getHeight() - 1);
    const int    x0 = int(x), y0 = int(y);
    const int    x1 = std::min(x0 + 1, map.getWidth() - 1), y1 = std::min(y0 + 1, map.getHeight() - 1);
    const double top    = std::lerp(double(map.height(x0, y0)), double(map.height(x1, y0)), x - x0);
    const double bottom = std::lerp(double(map.height(x0, y1)), double(map.height(x1, y1)), x - x0);
    return std::lerp(top, bottom, y - y0);
}

inline Result<int> publish(Heightmap& target, std::vector<float> candidate) {
    int changed = 0;
    for (size_t i = 0; i < candidate.size(); ++i) changed += candidate[i] != target.data()[i];
    target.data().swap(candidate);
    return Result<int>::success(changed);
}

// Clamp/Bilinear one-row LUT sampling; callers validate shape and finite inputs.
inline double curveSample(const Heightmap& curve, double value) {
    const double pixel = std::clamp(value * curve.getWidth() - 0.5, 0.0, double(curve.getWidth() - 1));
    const int    left = int(pixel), right = std::min(left + 1, curve.getWidth() - 1);
    return std::lerp(double(curve.height(left, 0)), double(curve.height(right, 0)), pixel - left);
}
}  // namespace eve::procgen::raster_detail
