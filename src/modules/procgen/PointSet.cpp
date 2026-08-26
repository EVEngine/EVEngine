#include "procgen/PointSet.h"

#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace eve::procgen {
namespace {

uint32_t mix32(uint32_t value) {
    value += 0x9e3779b9u;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}

float unitFloat(uint32_t seed) { return float(mix32(seed) >> 8u) * (1.f / 16777216.f); }

float pointSegmentDistanceSquared(float px, float pz, const ProcgenPoint& a,
                                  const ProcgenPoint& b) {
    const float vx      = b.x - a.x;
    const float vz      = b.z - a.z;
    const float length2 = vx * vx + vz * vz;
    const float t       = length2 > 0.f
                              ? std::clamp(((px - a.x) * vx + (pz - a.z) * vz) / length2, 0.f, 1.f)
                              : 0.f;
    const float dx = px - (a.x + vx * t);
    const float dz = pz - (a.z + vz * t);
    return dx * dx + dz * dz;
}

bool pointInPolygon(float x, float z, const std::vector<ProcgenPoint>& polygon) {
    bool inside = false;
    for (size_t i = 0, previous = polygon.size() - 1; i < polygon.size(); previous = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[previous];
        const bool crosses = ((a.z > z) != (b.z > z)) &&
                             (x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

}  // namespace

int  PointSet::getCount() const { return int(points_.size()); }
bool PointSet::empty() const { return points_.empty(); }
void PointSet::clear() { points_.clear(); }

int PointSet::add(float x, float y, float z) {
    ProcgenPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    points_.push_back(std::move(point));
    return int(points_.size()) - 1;
}

ProcgenPoint* PointSet::pointAt(int index) {
    return index >= 0 && index < int(points_.size()) ? &points_[size_t(index)] : nullptr;
}

const ProcgenPoint* PointSet::pointAt(int index) const {
    return index >= 0 && index < int(points_.size()) ? &points_[size_t(index)] : nullptr;
}

void PointSet::setPosition(int index, float x, float y, float z) {
    if (auto* point = pointAt(index)) {
        point->x = x;
        point->y = y;
        point->z = z;
    }
}

float PointSet::getX(int index) const {
    const auto* p = pointAt(index);
    return p ? p->x : 0.f;
}
float PointSet::getY(int index) const {
    const auto* p = pointAt(index);
    return p ? p->y : 0.f;
}
float PointSet::getZ(int index) const {
    const auto* p = pointAt(index);
    return p ? p->z : 0.f;
}

void PointSet::setNormal(int index, float x, float y, float z) {
    if (auto* point = pointAt(index)) {
        point->normalX = x;
        point->normalY = y;
        point->normalZ = z;
    }
}

float PointSet::getNormalX(int index) const {
    const auto* p = pointAt(index);
    return p ? p->normalX : 0.f;
}
float PointSet::getNormalY(int index) const {
    const auto* p = pointAt(index);
    return p ? p->normalY : 1.f;
}
float PointSet::getNormalZ(int index) const {
    const auto* p = pointAt(index);
    return p ? p->normalZ : 0.f;
}

void PointSet::setYaw(int index, float yaw) {
    if (auto* p = pointAt(index)) p->yaw = yaw;
}
float PointSet::getYaw(int index) const {
    const auto* p = pointAt(index);
    return p ? p->yaw : 0.f;
}

void PointSet::setScale(int index, float x, float y, float z) {
    if (auto* point = pointAt(index)) {
        point->scaleX = x;
        point->scaleY = y;
        point->scaleZ = z;
    }
}

float PointSet::getScaleX(int index) const {
    const auto* p = pointAt(index);
    return p ? p->scaleX : 1.f;
}
float PointSet::getScaleY(int index) const {
    const auto* p = pointAt(index);
    return p ? p->scaleY : 1.f;
}
float PointSet::getScaleZ(int index) const {
    const auto* p = pointAt(index);
    return p ? p->scaleZ : 1.f;
}

void PointSet::setDensity(int index, float density) {
    if (auto* point = pointAt(index)) point->density = std::clamp(density, 0.f, 1.f);
}

float PointSet::getDensity(int index) const {
    const auto* p = pointAt(index);
    return p ? p->density : 0.f;
}
void PointSet::setPointSeed(int index, uint32_t seed) {
    if (auto* p = pointAt(index)) p->seed = seed ? seed : 1u;
}
uint32_t PointSet::getPointSeed(int index) const {
    const auto* p = pointAt(index);
    return p ? p->seed : 0u;
}

void PointSet::setFloatAttribute(int index, const std::string& name, float value) {
    if (auto* point = pointAt(index); point && !name.empty()) point->floatAttributes[name] = value;
}

float PointSet::getFloatAttribute(int index, const std::string& name, float fallback) const {
    const auto* point = pointAt(index);
    if (!point) return fallback;
    const auto found = point->floatAttributes.find(name);
    return found == point->floatAttributes.end() ? fallback : found->second;
}

bool PointSet::hasFloatAttribute(int index, const std::string& name) const {
    const auto* point = pointAt(index);
    return point && point->floatAttributes.find(name) != point->floatAttributes.end();
}

void PointSet::setStringAttribute(int index, const std::string& name, const std::string& value) {
    if (auto* point = pointAt(index); point && !name.empty()) point->stringAttributes[name] = value;
}

std::string PointSet::getStringAttribute(int index, const std::string& name, const std::string& fallback) const {
    const auto* point = pointAt(index);
    if (!point) return fallback;
    const auto found = point->stringAttributes.find(name);
    return found == point->stringAttributes.end() ? fallback : found->second;
}

bool PointSet::hasStringAttribute(int index, const std::string& name) const {
    const auto* point = pointAt(index);
    return point && point->stringAttributes.find(name) != point->stringAttributes.end();
}

uint32_t deriveSeed(uint32_t parent, const std::string& scope) {
    uint32_t hash = 2166136261u ^ parent;
    for (const unsigned char ch : scope) {
        hash ^= ch;
        hash *= 16777619u;
    }
    hash = mix32(hash);
    return hash ? hash : 1u;
}

PointSet sampleGridPoints(int width, int depth, float spacing, uint32_t seed, float jitter) {
    PointSet output;
    if (width <= 0 || depth <= 0 || spacing <= 0.f) return output;
    jitter = std::clamp(jitter, 0.f, 1.f);
    output.points().reserve(size_t(width) * size_t(depth));
    const float extent = spacing * jitter * 0.5f;
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            const uint32_t pointSeed = mix32(seed ^ uint32_t(z * width + x));
            ProcgenPoint   point;
            point.x    = float(x) * spacing + (unitFloat(pointSeed) * 2.f - 1.f) * extent;
            point.z    = float(z) * spacing + (unitFloat(pointSeed ^ 0xa511e9b3u) * 2.f - 1.f) * extent;
            point.seed = pointSeed ? pointSeed : 1u;
            output.points().push_back(std::move(point));
        }
    }
    return output;
}

PointSet poissonDiskPoints(int width, int depth, float radius, uint32_t seed, int maxPoints) {
    PointSet output;
    if (width <= 0 || depth <= 0 || radius <= 0.f) return output;
    maxPoints = std::max(0, maxPoints);

    // Bridson's algorithm: candidate annulus [r, 2r], grid cell side r / sqrt(2).
    const float cell = radius * 0.70710678118f;
    const int   gridW = std::max(1, int(std::ceil(float(width) / cell)));
    const int   gridH = std::max(1, int(std::ceil(float(depth) / cell)));
    std::vector<int> grid(size_t(gridW) * size_t(gridH), -1);

    std::vector<float> xs, ys;
    std::mt19937       rng(seed);
    std::uniform_real_distribution<float> unit(0.f, 1.f);

    const auto cellIndex = [&](float x, float y) -> int {
        int gx = int(x / cell);
        int gy = int(y / cell);
        gx = std::max(0, std::min(gx, gridW - 1));
        gy = std::max(0, std::min(gy, gridH - 1));
        return gy * gridW + gx;
    };
    const auto inBounds = [&](float x, float y) { return x >= 0.f && x <= float(width) && y >= 0.f && y <= float(depth); };
    const auto tooClose = [&](float x, float y, float r2) {
        const int gx = std::max(0, std::min(int(x / cell), gridW - 1));
        const int gy = std::max(0, std::min(int(y / cell), gridH - 1));
        for (int oy = -2; oy <= 2; ++oy) {
            for (int ox = -2; ox <= 2; ++ox) {
                const int nx = gx + ox, ny = gy + oy;
                if (nx < 0 || nx >= gridW || ny < 0 || ny >= gridH) continue;
                const int idx = grid[ny * gridW + nx];
                if (idx < 0) continue;
                const float dx = x - xs[size_t(idx)], dy = y - ys[size_t(idx)];
                if (dx * dx + dy * dy < r2) return true;
            }
        }
        return false;
    };

    // Seed with one random interior point.
    float fx = unit(rng) * float(width);
    float fy = unit(rng) * float(depth);
    xs.push_back(fx);
    ys.push_back(fy);
    grid[cellIndex(fx, fy)] = 0;
    std::vector<int> active{0};

    const float r2 = radius * radius;
    constexpr int kTries = 30;
    while (!active.empty() && int(xs.size()) < maxPoints) {
        const int    pick = int(unit(rng) * float(active.size()));
        const int    base = active[size_t(std::min(pick, int(active.size()) - 1))];
        bool         found = false;
        for (int k = 0; k < kTries; ++k) {
            const float theta = unit(rng) * 6.28318530718f;
            const float dist  = radius * (1.f + unit(rng));
            const float nx    = xs[size_t(base)] + std::cos(theta) * dist;
            const float ny    = ys[size_t(base)] + std::sin(theta) * dist;
            if (!inBounds(nx, ny) || tooClose(nx, ny, r2)) continue;
            xs.push_back(nx);
            ys.push_back(ny);
            grid[cellIndex(nx, ny)] = int(xs.size()) - 1;
            active.push_back(int(xs.size()) - 1);
            found = true;
            break;
        }
        if (!found) active.erase(active.begin() + pick);
    }

    output.points().reserve(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) {
        ProcgenPoint point;
        point.x    = xs[i];
        point.y    = 0.f;
        point.z    = ys[i];
        point.seed = mix32(seed ^ uint32_t(i));
        if (point.seed == 0) point.seed = 1;
        output.points().push_back(std::move(point));
    }
    return output;
}

PointSet filterPointHeight(const PointSet& input, float minHeight, float maxHeight) {
    PointSet output;
    if (minHeight > maxHeight) std::swap(minHeight, maxHeight);
    for (const auto& point : input.points())
        if (point.y >= minHeight && point.y <= maxHeight) output.points().push_back(point);
    return output;
}

PointSet filterPointDensity(const PointSet& input, float minDensity, float maxDensity) {
    PointSet output;
    if (minDensity > maxDensity) std::swap(minDensity, maxDensity);
    for (const auto& point : input.points())
        if (point.density >= minDensity && point.density <= maxDensity) output.points().push_back(point);
    return output;
}

PointSet filterPointBox(const PointSet& input, float minX, float minY, float minZ, float maxX,
                        float maxY, float maxZ, bool invert) {
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);
    if (minZ > maxZ) std::swap(minZ, maxZ);
    PointSet output;
    for (const auto& point : input.points()) {
        const bool inside = point.x >= minX && point.x <= maxX && point.y >= minY &&
                            point.y <= maxY && point.z >= minZ && point.z <= maxZ;
        if (inside != invert) output.points().push_back(point);
    }
    return output;
}

PointSet filterPointSlope(const PointSet& input, float minDegrees, float maxDegrees) {
    if (minDegrees > maxDegrees) std::swap(minDegrees, maxDegrees);
    minDegrees = std::clamp(minDegrees, 0.f, 180.f);
    maxDegrees = std::clamp(maxDegrees, 0.f, 180.f);
    constexpr float radiansToDegrees = 57.29577951308232f;
    PointSet        output;
    for (const auto& point : input.points()) {
        const float length = std::sqrt(point.normalX * point.normalX + point.normalY * point.normalY +
                                       point.normalZ * point.normalZ);
        const float up     = length > 0.f ? std::clamp(point.normalY / length, -1.f, 1.f) : 1.f;
        const float slope  = std::acos(up) * radiansToDegrees;
        if (slope >= minDegrees && slope <= maxDegrees) output.points().push_back(point);
    }
    return output;
}

PointSet filterPointsByPolygon(const PointSet& input, const PointSet& polygon, bool invert) {
    PointSet output;
    if (polygon.points().size() < 3) return output;
    for (const auto& point : input.points()) {
        const bool inside = pointInPolygon(point.x, point.z, polygon.points());
        if (inside != invert) output.points().push_back(point);
    }
    return output;
}

PointSet filterPointsBySplineDistance(const PointSet& input, const PointSet& controlPoints,
                                      float minDistance, float maxDistance) {
    PointSet output;
    if (controlPoints.points().size() < 2) return output;
    if (minDistance > maxDistance) std::swap(minDistance, maxDistance);
    minDistance = std::max(0.f, minDistance);
    maxDistance = std::max(0.f, maxDistance);
    const float minSquared = minDistance * minDistance;
    const float maxSquared = maxDistance * maxDistance;
    for (const auto& point : input.points()) {
        float nearest = std::numeric_limits<float>::max();
        for (size_t i = 1; i < controlPoints.points().size(); ++i) {
            nearest = std::min(nearest, pointSegmentDistanceSquared(
                                            point.x, point.z, controlPoints.points()[i - 1],
                                            controlPoints.points()[i]));
        }
        if (nearest >= minSquared && nearest <= maxSquared) output.points().push_back(point);
    }
    return output;
}

PointSet excludePointRadius(const PointSet& input, float x, float z, float radius) {
    PointSet    output;
    const float radiusSquared = std::max(0.f, radius) * std::max(0.f, radius);
    for (const auto& point : input.points()) {
        const float dx = point.x - x;
        const float dz = point.z - z;
        if (dx * dx + dz * dz > radiusSquared) output.points().push_back(point);
    }
    return output;
}

PointSet jitterPointPositions(const PointSet& input, uint32_t seed, float amountX, float amountZ) {
    PointSet output = input;
    amountX         = std::max(0.f, amountX);
    amountZ         = std::max(0.f, amountZ);
    for (size_t i = 0; i < output.points().size(); ++i) {
        auto&          point      = output.points()[i];
        const uint32_t branchSeed = mix32(seed ^ point.seed ^ uint32_t(i));
        point.x += (unitFloat(branchSeed) * 2.f - 1.f) * amountX;
        point.z += (unitFloat(branchSeed ^ 0x63d83595u) * 2.f - 1.f) * amountZ;
    }
    return output;
}

PointSet selfPrunePoints(const PointSet& input, float radius) {
    if (radius <= 0.f) return input;
    PointSet    output;
    const float radiusSquared = radius * radius;
    for (const auto& candidate : input.points()) {
        bool keep = true;
        for (const auto& accepted : output.points()) {
            const float dx = candidate.x - accepted.x;
            const float dz = candidate.z - accepted.z;
            if (dx * dx + dz * dz < radiusSquared) {
                keep = false;
                break;
            }
        }
        if (keep) output.points().push_back(candidate);
    }
    return output;
}

PointSet projectPointsToHeightmap(const PointSet& input, const Heightmap& heightmap,
                                  float originX, float originZ, float cellSize,
                                  float heightScale) {
    if (cellSize <= 0.f || heightmap.getWidth() <= 0 || heightmap.getHeight() <= 0) return {};
    PointSet output = input;
    for (auto& point : output.points()) {
        const float hx = (point.x - originX) / cellSize;
        const float hz = (point.z - originZ) / cellSize;
        point.y        = heightmap.sampleBilinear(hx, hz) * heightScale;

        const float left  = heightmap.sampleBilinear(hx - 0.5f, hz) * heightScale;
        const float right = heightmap.sampleBilinear(hx + 0.5f, hz) * heightScale;
        const float down  = heightmap.sampleBilinear(hx, hz - 0.5f) * heightScale;
        const float up    = heightmap.sampleBilinear(hx, hz + 0.5f) * heightScale;
        float       nx    = -(right - left) / cellSize;
        float       ny    = 1.f;
        float       nz    = -(up - down) / cellSize;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0.f) {
            nx /= length;
            ny /= length;
            nz /= length;
        }
        point.normalX = nx;
        point.normalY = ny;
        point.normalZ = nz;
    }
    return output;
}

PointSet samplePolylinePoints(const PointSet& controlPoints, float spacing, uint32_t seed,
                              float lateralJitter) {
    PointSet output;
    if (controlPoints.points().size() < 2 || spacing <= 0.f) return output;
    lateralJitter = std::max(0.f, lateralJitter);
    float distanceToNext = 0.f;
    uint32_t sampleIndex = 0;
    for (size_t segment = 1; segment < controlPoints.points().size(); ++segment) {
        const auto& a      = controlPoints.points()[segment - 1];
        const auto& b      = controlPoints.points()[segment];
        const float dx     = b.x - a.x;
        const float dy     = b.y - a.y;
        const float dz     = b.z - a.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length <= 0.f) continue;
        const float nx  = -dz / length;
        const float nz  = dx / length;
        const float yaw = std::atan2(dz, dx) * 57.29577951308232f;
        while (distanceToNext <= length) {
            const float    t           = distanceToNext / length;
            const uint32_t sampleSeed  = mix32(seed ^ sampleIndex);
            const float    lateral     = (unitFloat(sampleSeed) * 2.f - 1.f) * lateralJitter;
            ProcgenPoint   point;
            point.x    = a.x + dx * t + nx * lateral;
            point.y    = a.y + dy * t;
            point.z    = a.z + dz * t + nz * lateral;
            point.yaw  = yaw;
            point.seed = sampleSeed ? sampleSeed : 1u;
            output.points().push_back(std::move(point));
            ++sampleIndex;
            distanceToNext += spacing;
        }
        distanceToNext -= length;
    }
    return output;
}

}  // namespace eve::procgen
