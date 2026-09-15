#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <numbers>

namespace eve::procgen {
Result<float> sampleTerrainHeightmapSafe(const Heightmap& source, int x, int z) {
    if (!raster_detail::validRaster(source))
        return Result<float>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.heightmapSafeSample: finite data required"));
    return Result<float>::success(source.height(std::clamp(x, 0, source.getWidth() - 1),
                                                std::clamp(z, 0, source.getHeight() - 1)));
}

Result<float> sampleTerrainHeightmapNormalized(const Heightmap& source, float x, float z) {
    if (!raster_detail::validRaster(source) || !std::isfinite(x) || !std::isfinite(z) || x < 0 || x > 1 || z < 0 ||
        z > 1)
        return Result<float>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "terrain.heightmapNormalizedSample: finite data and coordinates in [0,1] required"));
    const double px = double(x) * source.getWidth(), pz = double(z) * source.getHeight();
    const int x0 = std::min(int(px), source.getWidth() - 1), z0 = std::min(int(pz), source.getHeight() - 1);
    const int x1 = std::min(x0 + 1, source.getWidth() - 1), z1 = std::min(z0 + 1, source.getHeight() - 1);
    const double tx = px - x0, tz = pz - z0;
    const double value = (1 - tx) * (1 - tz) * source.height(x0, z0) +
                         (1 - tx) * tz * source.height(x0, z1) + tx * (1 - tz) * source.height(x1, z0) +
                         tx * tz * source.height(x1, z1);
    return Result<float>::success(float(value));
}

Result<bool> terrainHeightmapHasData(const Heightmap& source) {
    if (source.getWidth() == 0 && source.getHeight() == 0 && source.data().empty()) return Result<bool>::success(false);
    if (source.getWidth() <= 0 || source.getHeight() <= 0 ||
        source.data().size() != size_t(source.getWidth()) * size_t(source.getHeight()))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.heightmapHasData: malformed storage"));
    return Result<bool>::success(true);
}

Result<bool> terrainHeightmapIsPowerOfTwo(const Heightmap& source) {
    auto hasData = terrainHeightmapHasData(source);
    if (!hasData.ok()) return Result<bool>::failure(hasData.status());
    if (!hasData.value()) return Result<bool>::success(false);
    const auto power = [](int value) { return value > 0 && (value & (value - 1)) == 0; };
    return Result<bool>::success(power(source.getWidth()) && power(source.getHeight()));
}
namespace {
bool compatible(const Heightmap& target, const Heightmap& source) {
    return raster_detail::validRaster(target) && raster_detail::validRaster(source) && source.getWidth() >= 2 &&
           source.getHeight() >= 2 && target.getWidth() == source.getWidth() && target.getHeight() == source.getHeight();
}
double sign(double value) { return value > 0 ? 1.0 : value < 0 ? -1.0 : 0.0; }
}

Result<int> generateTerrainHeightmapCurvature(Heightmap& target, const Heightmap& source,
                                               TerrainHeightmapCurvature mode) {
    using namespace raster_detail;
    if (!compatible(target, source) || mode < TerrainHeightmapCurvature::Average ||
        mode > TerrainHeightmapCurvature::Vertical)
        return invalid("terrain.heightmapCurvature: matching finite 2D rasters and known mode required");
    constexpr double limit = 10000.0;
    const int width = source.getWidth(), height = source.getHeight();
    const double ux = 1.0 / (width - 1.0), uy = 1.0 / (height - 1.0);
    const auto input = source.data();
    std::vector<float> output(input.size());
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    auto normalized = [limit](double value) {
        if (!std::isfinite(value)) value = 0;
        return std::clamp(value, -limit, limit) / limit * 0.5 + 0.5;
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int xm = std::max(x - 1, 0), xp = std::min(x + 1, width - 1);
            const int ym = std::max(y - 1, 0), yp = std::min(y + 1, height - 1);
            const double value = input[at(x, y)], left = input[at(xm, y)], right = input[at(xp, y)];
            const double bottom = input[at(x, ym)], top = input[at(x, yp)];
            const double leftBottom = input[at(xm, ym)], leftTop = input[at(xm, yp)];
            const double rightBottom = input[at(xp, ym)], rightTop = input[at(xp, yp)];
            const double dx = (right - left) / (2 * ux), dy = (top - bottom) / (2 * uy);
            const double dxx = (right - 2 * value + left) / (ux * ux);
            const double dyy = (top - 2 * value + bottom) / (uy * uy);
            const double dxy = (rightTop - rightBottom - leftTop + leftBottom) / (4 * ux * uy);
            const double denominator = dx * dx + dy * dy;
            const double horizontal = normalized(-2 * (dy * dy * dxx + dx * dx * dyy - dx * dy * dxy) / denominator);
            const double vertical = normalized(-2 * (dx * dx * dxx + dy * dy * dyy + dx * dy * dxy) / denominator);
            output[at(x, y)] = float(mode == TerrainHeightmapCurvature::Horizontal
                                         ? horizontal
                                         : mode == TerrainHeightmapCurvature::Vertical ? vertical
                                                                                       : (horizontal + vertical) * 0.5);
        }
    return publish(target, std::move(output));
}

Result<int> generateTerrainHeightmapAspect(Heightmap& target, const Heightmap& source, TerrainHeightmapAspect mode) {
    using namespace raster_detail;
    if (!compatible(target, source) || mode < TerrainHeightmapAspect::Aspect || mode > TerrainHeightmapAspect::Easterness)
        return invalid("terrain.heightmapAspect: matching finite 2D rasters and known mode required");
    const int width = source.getWidth(), height = source.getHeight();
    const double ux = 1.0 / (width - 1.0), uy = 1.0 / (height - 1.0);
    const auto input = source.data();
    std::vector<float> output(input.size());
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    constexpr double radians = std::numbers::pi / 180.0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int xm = std::max(x - 1, 0), xp = std::min(x + 1, width - 1);
            const int ym = std::max(y - 1, 0), yp = std::min(y + 1, height - 1);
            const double dx = (double(input[at(xp, y)]) - input[at(xm, y)]) / (2 * ux);
            const double dy = (double(input[at(x, yp)]) - input[at(x, ym)]) / (2 * uy);
            const double magnitude = std::sqrt(dx * dx + dy * dy);
            double angle = std::acos(-dy / magnitude) / radians;
            if (!std::isfinite(angle)) angle = 0;
            double aspect = 180.0 * (1.0 + sign(dx)) - sign(dx) * angle;
            if (mode == TerrainHeightmapAspect::Northerness) aspect = std::cos(aspect * radians) * 0.5 + 0.5;
            else if (mode == TerrainHeightmapAspect::Easterness) aspect = std::sin(aspect * radians) * 0.5 + 0.5;
            else aspect /= 360.0;
            if (!isRepresentable(aspect)) return invalid("terrain.heightmapAspect: derived value exceeds float range");
            output[at(x, y)] = float(aspect);
        }
    return publish(target, std::move(output));
}

Result<int> filterTerrainHeightmapNeighborhood(Heightmap& target, const Heightmap& source, int radius,
                                                TerrainHeightmapNeighborhood mode) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || radius < 0 || radius > std::max(source.getWidth(), source.getHeight()) ||
        (mode == TerrainHeightmapNeighborhood::DeNoise && radius == 0) ||
        mode < TerrainHeightmapNeighborhood::DeNoise || mode > TerrainHeightmapNeighborhood::ShrinkEdges)
        return invalid("terrain.heightmapNeighborhood: matching finite rasters, valid radius and known mode required");
    const int width = source.getWidth(), height = source.getHeight();
    auto output = source.data();
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    const int beginX = mode == TerrainHeightmapNeighborhood::DeNoise ? radius : 0;
    const int endX = mode == TerrainHeightmapNeighborhood::DeNoise ? width - radius : width;
    const int beginY = mode == TerrainHeightmapNeighborhood::DeNoise ? radius : 0;
    const int endY = mode == TerrainHeightmapNeighborhood::DeNoise ? height - radius : height;
    for (int x = beginX; x < endX; ++x)
        for (int y = beginY; y < endY; ++y) {
            float minimum = std::numeric_limits<float>::max();
            float maximum = std::numeric_limits<float>::lowest();
            for (int dx = -radius; dx <= radius; ++dx) {
                const int nx = x + dx;
                if (nx < 0 || nx >= width) continue;
                for (int dy = -radius; dy <= radius; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    const int ny = y + dy;
                    if (ny < 0 || ny >= height) continue;
                    const float neighbor = output[at(nx, ny)];
                    minimum = std::min(minimum, neighbor); maximum = std::max(maximum, neighbor);
                }
            }
            float& value = output[at(x, y)];
            if (mode == TerrainHeightmapNeighborhood::DeNoise) value = std::clamp(value, minimum, maximum);
            else if (mode == TerrainHeightmapNeighborhood::GrowEdges && maximum > value) value = (maximum + value) * 0.5F;
            else if (mode == TerrainHeightmapNeighborhood::ShrinkEdges && minimum < value) value = (minimum + value) * 0.5F;
        }
    return publish(target, std::move(output));
}

Result<int> smoothTerrainHeightmap(Heightmap& target, const Heightmap& source, int iterations) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || iterations < 0)
        return invalid("terrain.heightmapSmooth: matching finite rasters and nonnegative iterations required");
    const int width = source.getWidth(), height = source.getHeight();
    auto output = source.data();
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    for (int pass = 0; pass < iterations; ++pass)
        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y) {
                const int left = std::max(0, x - 1), right = std::min(width - 1, x + 1);
                const int bottom = std::max(0, y - 1), top = std::min(height - 1, y + 1);
                output[at(x, y)] = std::clamp((output[at(left, y)] + output[at(right, y)] +
                                                output[at(x, bottom)] + output[at(x, top)]) * 0.25F,
                                               0.F, 1.F);
            }
    return publish(target, std::move(output));
}

Result<int> smoothTerrainHeightmapRadius(Heightmap& target, const Heightmap& source, int radius) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || radius < 0)
        return invalid("terrain.heightmapSmoothRadius: matching finite rasters and nonnegative radius required");
    radius = std::max(5, radius);
    const int width = source.getWidth(), height = source.getHeight();
    auto output = source.data();
    if (radius < width && radius < height) {
        const float factor = 1.F / float((2 * radius + 1) * (2 * radius + 1));
        std::vector<float> filter = source.data();
        for (float& value : filter) value *= factor;
        auto at = [width](int x, int y) { return size_t(y) * width + x; };
        for (int x = radius; x < width - radius; ++x) {
            int y = radius;
            float sum = 0.F;
            for (int i = -radius; i <= radius; ++i)
                for (int j = -radius; j <= radius; ++j) sum += filter[at(x + j, y + i)];
            for (++y; y < height - radius; ++y) {
                for (int j = -radius; j <= radius; ++j) {
                    sum -= filter[at(x + j, y - radius - 1)];
                    sum += filter[at(x + j, y + radius)];
                }
                output[at(x, y)] = sum;
            }
        }
    }
    return publish(target, std::move(output));
}

Result<int> convolveTerrainHeightmap(Heightmap& target, const Heightmap& source, const Heightmap& kernel) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || !validRaster(kernel) ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight() ||
        kernel.getWidth() != kernel.getHeight() || kernel.getWidth() % 2 == 0)
        return invalid("terrain.heightmapConvolve: matching finite rasters and an odd square finite kernel required");
    const int width = source.getWidth(), height = source.getHeight(), kernelSize = kernel.getWidth();
    const int radius = kernelSize / 2;
    auto output = source.data();
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    double divisor = 0.0;
    for (float value : kernel.data()) divisor += value;
    if (std::abs(divisor) <= 0.000001) divisor = 1.0;
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            if (x < radius || y < radius || x + radius >= width || y + radius >= height) continue;
            double sum = 0.0;
            for (int r = -radius; r <= radius; ++r)
                for (int j = -radius; j <= radius; ++j)
                    sum += double(output[at(x + r, y + j)]) * kernel.height(r + radius, j + radius);
            if (!isRepresentable(sum / divisor)) return invalid("terrain.heightmapConvolve: result exceeds float range");
            output[at(x, y)] = std::clamp(float(sum / divisor), 0.F, 1.F);
        }
    return publish(target, std::move(output));
}

Result<int> generateTerrainHeightmapSlope(Heightmap& target, const Heightmap& source) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || source.getWidth() < 2 || source.getHeight() < 2)
        return invalid("terrain.heightmapSlope: matching finite rasters with dimensions at least two required");
    const int width = source.getWidth(), height = source.getHeight();
    std::vector<float> output(size_t(width) * height);
    auto at = [width](int x, int y) { return size_t(y) * width + x; };
    const double ux = 1.0 / double(width - 1), uy = 1.0 / double(height - 1);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int xp1 = x == width - 1 ? x : x + 1, xn1 = x == 0 ? x : x - 1;
            const int yp1 = y == height - 1 ? y : y + 1, yn1 = y == 0 ? y : y - 1;
            const double dx = (double(source.height(xp1, y)) * 0.5 - double(source.height(xn1, y)) * 0.5) /
                              (2.0 * ux);
            const double dy = (double(source.height(x, yp1)) * 0.5 - double(source.height(x, yn1)) * 0.5) /
                              (2.0 * uy);
            const double gradient = std::sqrt(dx * dx + dy * dy);
            const double slope = gradient / std::sqrt(1.0 + gradient * gradient);
            if (!isRepresentable(slope)) return invalid("terrain.heightmapSlope: derived value exceeds float range");
            output[at(x, y)] = float(slope);
        }
    return publish(target, std::move(output));
}

Result<int> quantizeTerrainHeightmap(Heightmap& target, const Heightmap& source, float divisor) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || !std::isfinite(divisor) || divisor == 0.F)
        return invalid("terrain.heightmapQuantize: matching finite rasters and finite nonzero divisor required");
    auto output = source.data();
    for (float& value : output) {
        const double quantized = std::nearbyint(double(value) / divisor) * divisor;
        if (!isRepresentable(quantized)) return invalid("terrain.heightmapQuantize: result exceeds float range");
        value = float(quantized);
    }
    return publish(target, std::move(output));
}

namespace {
bool knownArithmetic(TerrainHeightmapArithmetic operation) {
    return operation >= TerrainHeightmapArithmetic::Add && operation <= TerrainHeightmapArithmetic::Divide;
}
double arithmetic(double left, double right, TerrainHeightmapArithmetic operation) {
    if (operation == TerrainHeightmapArithmetic::Add) return left + right;
    if (operation == TerrainHeightmapArithmetic::Subtract) return left - right;
    if (operation == TerrainHeightmapArithmetic::Multiply) return left * right;
    return left / right;
}
float pcgResample(const Heightmap& raster, int x, int y, int targetWidth, int targetHeight) {
    if (raster.getWidth() == targetWidth && raster.getHeight() == targetHeight) return raster.height(x, y);
    return raster.sampleBilinear(float(x) * raster.getWidth() / targetWidth,
                                 float(y) * raster.getHeight() / targetHeight);
}
}

Result<int> applyTerrainHeightmapScalarArithmetic(Heightmap& target, const Heightmap& source, float operand,
                                                   TerrainHeightmapArithmetic operation, bool clampResult,
                                                   float minValue, float maxValue) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || !std::isfinite(operand) || !knownArithmetic(operation) ||
        (operation == TerrainHeightmapArithmetic::Divide && operand == 0.F) ||
        (clampResult && (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue > maxValue)))
        return invalid("terrain.heightmapScalarArithmetic: finite matching inputs, valid operation and clamp required");
    auto output = source.data();
    for (float& value : output) {
        double result = arithmetic(value, operand, operation);
        if (clampResult) result = std::clamp(result, double(minValue), double(maxValue));
        if (!isRepresentable(result)) return invalid("terrain.heightmapScalarArithmetic: result exceeds float range");
        value = float(result);
    }
    return publish(target, std::move(output));
}

Result<int> applyTerrainHeightmapRasterArithmetic(Heightmap& target, const Heightmap& source,
                                                   const Heightmap& operand, TerrainHeightmapArithmetic operation,
                                                   bool clampResult, float minValue, float maxValue) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || !validRaster(operand) ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight() ||
        !knownArithmetic(operation) ||
        (clampResult && (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue > maxValue)))
        return invalid("terrain.heightmapRasterArithmetic: finite inputs, valid operation and clamp required");
    const int width = source.getWidth(), height = source.getHeight();
    auto output = source.data();
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            const float right = pcgResample(operand, x, y, width, height);
            if (operation == TerrainHeightmapArithmetic::Divide && right == 0.F)
                return invalid("terrain.heightmapRasterArithmetic: division by zero");
            double result = arithmetic(source.height(x, y), right, operation);
            if (clampResult) result = std::clamp(result, double(minValue), double(maxValue));
            if (!isRepresentable(result)) return invalid("terrain.heightmapRasterArithmetic: result exceeds float range");
            output[size_t(y) * width + x] = float(result);
        }
    return publish(target, std::move(output));
}

Result<int> lerpTerrainHeightmap(Heightmap& target, const Heightmap& source, const Heightmap& values,
                                 const Heightmap& mask) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || !validRaster(values) || !validRaster(mask) ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight())
        return invalid("terrain.heightmapLerp: finite inputs and target matching source required");
    const int width = source.getWidth(), height = source.getHeight();
    auto output = source.data();
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            const double amount = std::clamp(double(pcgResample(mask, x, y, width, height)), 0.0, 1.0);
            const double start = source.height(x, y), end = pcgResample(values, x, y, width, height);
            const double result = start + (end - start) * amount;
            if (!isRepresentable(result)) return invalid("terrain.heightmapLerp: result exceeds float range");
            output[size_t(y) * width + x] = float(result);
        }
    return publish(target, std::move(output));
}

Result<int> transformTerrainHeightmap(Heightmap& target, const Heightmap& source,
                                      TerrainHeightmapTransform transform, float parameter) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || target.getWidth() != source.getWidth() ||
        target.getHeight() != source.getHeight() || transform < TerrainHeightmapTransform::Invert ||
        transform > TerrainHeightmapTransform::Contrast ||
        ((transform == TerrainHeightmapTransform::Power || transform == TerrainHeightmapTransform::Contrast) &&
         !std::isfinite(parameter)))
        return invalid("terrain.heightmapTransform: matching finite rasters, known transform and parameter required");
    auto output = source.data();
    float minimum = 0.F, range = 0.F;
    if (transform == TerrainHeightmapTransform::Normalise) {
        const auto [minIt, maxIt] = std::minmax_element(output.begin(), output.end());
        minimum = *minIt;
        range = *maxIt - minimum;
    }
    for (float& value : output) {
        double result = value;
        if (transform == TerrainHeightmapTransform::Invert) result = 1.0 - value;
        else if (transform == TerrainHeightmapTransform::Normalise && range > 0.F) result = (value - minimum) / range;
        else if (transform == TerrainHeightmapTransform::Power) result = std::pow(double(value), parameter);
        else if (transform == TerrainHeightmapTransform::Contrast) result = (double(value) - 0.5) * parameter + 0.5;
        if (!isRepresentable(result)) return invalid("terrain.heightmapTransform: result exceeds float range");
        value = float(result);
    }
    return publish(target, std::move(output));
}

Result<int> copyTerrainHeightmap(Heightmap& target, const Heightmap& source, TerrainHeightmapCopy mode) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || mode < TerrainHeightmapCopy::Always ||
        mode > TerrainHeightmapCopy::IfGreater)
        return invalid("terrain.heightmapCopy: finite rasters and known copy mode required");
    const int width = target.getWidth(), height = target.getHeight();
    auto output = target.data();
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y) {
            const size_t index = size_t(y) * width + x;
            const float candidate = pcgResample(source, x, y, width, height);
            if (mode == TerrainHeightmapCopy::Always ||
                (mode == TerrainHeightmapCopy::IfLess && candidate < output[index]) ||
                (mode == TerrainHeightmapCopy::IfGreater && candidate > output[index]))
                output[index] = candidate;
        }
    return publish(target, std::move(output));
}

Result<int> copyTerrainHeightmapClamped(Heightmap& target, const Heightmap& source, float minValue, float maxValue) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || !std::isfinite(minValue) || !std::isfinite(maxValue) ||
        minValue > maxValue)
        return invalid("terrain.heightmapCopyClamped: finite rasters and ordered finite bounds required");
    const int width = target.getWidth(), height = target.getHeight();
    std::vector<float> output(size_t(width) * height);
    for (int x = 0; x < width; ++x)
        for (int y = 0; y < height; ++y)
            output[size_t(y) * width + x] =
                std::clamp(pcgResample(source, x, y, width, height), minValue, maxValue);
    return publish(target, std::move(output));
}

Result<int> flipTerrainHeightmap(Heightmap& target, const Heightmap& source) {
    using namespace raster_detail;
    if (!validRaster(source)) return invalid("terrain.heightmapFlip: finite source required");
    const auto input = source.data();
    const int oldWidth = source.getWidth(), oldHeight = source.getHeight();
    Heightmap candidate(oldHeight, oldWidth);
    for (int x = 0; x < oldWidth; ++x)
        for (int y = 0; y < oldHeight; ++y) candidate.setHeight(y, x, input[size_t(y) * oldWidth + x]);
    int changed = int(candidate.data().size());
    if (target.getWidth() == candidate.getWidth() && target.getHeight() == candidate.getHeight() &&
        target.data().size() == candidate.data().size()) {
        changed = 0;
        for (size_t i = 0; i < candidate.data().size(); ++i) changed += target.data()[i] != candidate.data()[i];
    }
    target = std::move(candidate);
    return Result<int>::success(changed);
}

Result<double> measureTerrainHeightmap(const Heightmap& source, TerrainHeightmapMeasure measure) {
    using namespace raster_detail;
    if (!validRaster(source) || measure < TerrainHeightmapMeasure::Minimum ||
        measure > TerrainHeightmapMeasure::BaseLevel)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.heightmapMeasure: finite raster and known measure required"));
    if (measure == TerrainHeightmapMeasure::Minimum)
        return Result<double>::success(*std::min_element(source.data().begin(), source.data().end()));
    if (measure == TerrainHeightmapMeasure::Maximum)
        return Result<double>::success(*std::max_element(source.data().begin(), source.data().end()));
    if (measure == TerrainHeightmapMeasure::BaseLevel) {
        float level = 0.F;
        const int width = source.getWidth(), height = source.getHeight();
        for (int x = 0; x < width; ++x) level = std::max({level, source.height(x, 0), source.height(x, height - 1)});
        for (int y = 0; y < height; ++y) level = std::max({level, source.height(0, y), source.height(width - 1, y)});
        return Result<double>::success(level);
    }
    float sum = 0.F;
    for (int x = 0; x < source.getWidth(); ++x)
        for (int y = 0; y < source.getHeight(); ++y) sum += source.height(x, y);
    if (!std::isfinite(sum))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.heightmapMeasure: float sum overflow"));
    if (measure == TerrainHeightmapMeasure::Average) sum /= float(source.getWidth() * source.getHeight());
    return Result<double>::success(sum);
}

Result<int> quantizeTerrainHeightmapTerraces(Heightmap& target, const Heightmap& source,
                                              const Heightmap& startHeights, const Heightmap& curves) {
    using namespace raster_detail;
    if (!validRaster(target) || !validRaster(source) || !validRaster(startHeights) || !validRaster(curves) ||
        target.getWidth() != source.getWidth() || target.getHeight() != source.getHeight() ||
        startHeights.getHeight() != 1 || curves.getHeight() != startHeights.getWidth() || curves.getWidth() < 2)
        return invalid("terrain.heightmapTerraceQuantize: matching finite rasters, N-by-1 starts and curve rows required");
    const int count = startHeights.getWidth();
    for (int i = 0; i < count; ++i) {
        const float start = startHeights.height(i, 0);
        if (start > 1.F || (i > 0 && start <= startHeights.height(i - 1, 0)))
            return invalid("terrain.heightmapTerraceQuantize: starts must be strictly increasing and at most one");
    }
    if (startHeights.height(count - 1, 0) >= 1.F)
        return invalid("terrain.heightmapTerraceQuantize: final terrace must start below one");
    auto output = source.data();
    for (float& value : output) {
        int terrace = count - 1;
        while (terrace >= 0) {
            const float start = startHeights.height(terrace, 0);
            const float next = terrace == count - 1 ? 1.F : startHeights.height(terrace + 1, 0);
            if (start <= value && value <= next) break;
            --terrace;
        }
        if (terrace < 0)
            return invalid("terrain.heightmapTerraceQuantize: source height is outside terrace coverage");
        const double start = startHeights.height(terrace, 0);
        const double next = terrace == count - 1 ? 1.0 : startHeights.height(terrace + 1, 0);
        if (next <= start) return invalid("terrain.heightmapTerraceQuantize: final terrace must start below one");
        const double t = (value - start) / (next - start);
        const double x = t * (curves.getWidth() - 1);
        const int x0 = int(x), x1 = std::min(x0 + 1, curves.getWidth() - 1);
        const double shaped = std::lerp(double(curves.height(x0, terrace)), double(curves.height(x1, terrace)), x - x0);
        const double result = start + (value - start) * shaped;
        if (!isRepresentable(result)) return invalid("terrain.heightmapTerraceQuantize: result exceeds float range");
        value = float(result);
    }
    return publish(target, std::move(output));
}

Result<double> measureTerrainHeightmapSlope(const Heightmap& source, float x, float y,
                                             TerrainHeightmapSlopeQuery mode) {
    using namespace raster_detail;
    if (!validRaster(source) || source.getWidth() < 2 || source.getHeight() < 2 || !std::isfinite(x) ||
        !std::isfinite(y) || mode < TerrainHeightmapSlopeQuery::GridForward ||
        mode > TerrainHeightmapSlopeQuery::NormalizedAverage)
        return Result<double>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.heightmapSlopeQuery: finite 2D raster, coordinates and known mode required"));
    if (mode == TerrainHeightmapSlopeQuery::GridForward) {
        const int ix = int(x), iy = int(y);
        if (x != ix || y != iy || ix < 0 || iy < 0 || ix >= source.getWidth() - 1 || iy >= source.getHeight() - 1)
            return Result<double>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.heightmapSlopeQuery: forward mode requires an interior integer coordinate"));
        const double center = source.height(ix, iy);
        const double dx = source.height(ix + 1, iy) - center, dy = source.height(ix, iy + 1) - center;
        return Result<double>::success(std::sqrt(dx * dx + dy * dy));
    }
    if (x < 0.F || x > 1.F || y < 0.F || y > 1.F)
        return Result<double>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.heightmapSlopeQuery: normalized coordinates must be in [0,1]"));
    auto sampleNormalized = [&source](double u, double v) {
        return double(source.sampleBilinear(float(u * source.getWidth()), float(v * source.getHeight())));
    };
    const double invX = 1.0 / source.getWidth(), invY = 1.0 / source.getHeight();
    if (mode == TerrainHeightmapSlopeQuery::NormalizedCentral) {
        const double dx = sampleNormalized(x + invX * 0.9, y) - sampleNormalized(x - invX * 0.9, y);
        const double dy = sampleNormalized(x, y + invY * 0.9) - sampleNormalized(x, y - invY * 0.9);
        return Result<double>::success(std::clamp(std::sqrt(dx * dx + dy * dy) * 10000.0, 0.0, 90.0));
    }
    const double center = sampleNormalized(x, y);
    const double difference = std::abs(sampleNormalized(x - invX, y) - center) +
                              std::abs(sampleNormalized(x + invX, y) - center) +
                              std::abs(sampleNormalized(x, y - invY) - center) +
                              std::abs(sampleNormalized(x, y + invY) - center);
    return Result<double>::success(difference * 100.0);
}

Result<int> fillTerrainHeightmap(Heightmap& target, float value) {
    using namespace raster_detail;
    if (!validRaster(target) || !std::isfinite(value))
        return invalid("terrain.heightmapFill: finite target and value required");
    std::vector<float> output(target.data().size(), std::clamp(value, 0.F, 1.F));
    return publish(target, std::move(output));
}

Result<int> setTerrainHeightmapSafe(Heightmap& target, int x, int y, float value) {
    using namespace raster_detail;
    if (!validRaster(target) || !std::isfinite(value))
        return invalid("terrain.heightmapSetSafe: finite target and value required");
    x = std::clamp(x, 0, target.getWidth() - 1);
    y = std::clamp(y, 0, target.getHeight() - 1);
    auto output = target.data();
    output[size_t(y) * target.getWidth() + x] = value;
    return publish(target, std::move(output));
}

namespace {
bool validStrip(const Heightmap& values, int length) {
    return raster_detail::validRaster(values) && (values.getWidth() == 1 || values.getHeight() == 1) &&
           int(values.data().size()) == length;
}
}

Result<int> setTerrainHeightmapRow(Heightmap& target, int rowX, const Heightmap& values) {
    using namespace raster_detail;
    if (!validRaster(target) || !validStrip(values, target.getHeight()) || rowX < 0 || rowX >= target.getWidth())
        return invalid("terrain.heightmapSetRow: finite target, valid X and target-height strip required");
    const auto strip = values.data();
    auto output = target.data();
    for (int y = 0; y < target.getHeight(); ++y) output[size_t(y) * target.getWidth() + rowX] = strip[size_t(y)];
    return publish(target, std::move(output));
}

Result<int> setTerrainHeightmapColumn(Heightmap& target, int columnZ, const Heightmap& values) {
    using namespace raster_detail;
    if (!validRaster(target) || !validStrip(values, target.getWidth()) || columnZ < 0 ||
        columnZ >= target.getHeight())
        return invalid("terrain.heightmapSetColumn: finite target, valid Z and target-width strip required");
    const auto strip = values.data();
    auto output = target.data();
    for (int x = 0; x < target.getWidth(); ++x) output[size_t(columnZ) * target.getWidth() + x] = strip[size_t(x)];
    return publish(target, std::move(output));
}

Result<int> resetTerrainHeightmap(Heightmap& target) {
    const int changed = int(target.data().size());
    target.resize(0, 0);
    return Result<int>::success(changed);
}
}
