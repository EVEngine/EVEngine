#include "procgen/PointSet.h"

#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

PointSet mergePointSets(const PointSet& first, const PointSet& second) {
    PointSet output;
    output.points().reserve(first.points().size() + second.points().size());
    output.points().insert(output.points().end(), first.points().begin(), first.points().end());
    output.points().insert(output.points().end(), second.points().begin(), second.points().end());
    return output;
}

PointSet transformPointSet(const PointSet& input, float translateX, float translateY,
                           float translateZ, float yawDegrees, float scaleX, float scaleY,
                           float scaleZ) {
    PointSet output = input;
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float     radians          = yawDegrees * degreesToRadians;
    const float     cosine           = std::cos(radians);
    const float     sine             = std::sin(radians);
    for (auto& point : output.points()) {
        const float x = point.x * scaleX;
        const float z = point.z * scaleZ;
        point.x       = x * cosine - z * sine + translateX;
        point.y       = point.y * scaleY + translateY;
        point.z       = x * sine + z * cosine + translateZ;
        point.yaw += yawDegrees;
        point.scaleX *= scaleX;
        point.scaleY *= scaleY;
        point.scaleZ *= scaleZ;

        const float nx = point.normalX / (std::abs(scaleX) > 0.000001f ? scaleX : 1.f);
        const float ny = point.normalY / (std::abs(scaleY) > 0.000001f ? scaleY : 1.f);
        const float nz = point.normalZ / (std::abs(scaleZ) > 0.000001f ? scaleZ : 1.f);
        point.normalX  = nx * cosine - nz * sine;
        point.normalY  = ny;
        point.normalZ  = nx * sine + nz * cosine;
        const float normalLength = std::sqrt(point.normalX * point.normalX +
                                             point.normalY * point.normalY +
                                             point.normalZ * point.normalZ);
        if (normalLength > 0.f) {
            point.normalX /= normalLength;
            point.normalY /= normalLength;
            point.normalZ /= normalLength;
        }
    }
    return output;
}

PointSet copyPointsToTargets(const PointSet& source, const PointSet& targets,
                             bool inheritTargetAttributes) {
    constexpr float degreesToRadians = 0.017453292519943295f;
    PointSet result;
    if (!targets.points().empty() &&
        source.points().size() >
            std::numeric_limits<size_t>::max() / targets.points().size())
        return result;
    result.points().reserve(source.points().size() * targets.points().size());
    for (const auto& target : targets.points()) {
        const float radians = target.yaw * degreesToRadians;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        for (const auto& sourcePoint : source.points()) {
            ProcgenPoint copy = sourcePoint;
            const float localX = sourcePoint.x * target.scaleX;
            const float localY = sourcePoint.y * target.scaleY;
            const float localZ = sourcePoint.z * target.scaleZ;
            copy.x = target.x + localX * cosine - localZ * sine;
            copy.y = target.y + localY;
            copy.z = target.z + localX * sine + localZ * cosine;
            const float normalX = sourcePoint.normalX * cosine - sourcePoint.normalZ * sine;
            const float normalZ = sourcePoint.normalX * sine + sourcePoint.normalZ * cosine;
            copy.normalX = normalX;
            copy.normalZ = normalZ;
            copy.yaw += target.yaw;
            copy.scaleX *= target.scaleX;
            copy.scaleY *= target.scaleY;
            copy.scaleZ *= target.scaleZ;
            copy.density *= target.density;
            copy.seed = deriveSeed(target.seed, "copy:" + std::to_string(sourcePoint.seed));
            if (inheritTargetAttributes) {
                auto sourceFloats = std::move(copy.floatAttributes);
                auto sourceStrings = std::move(copy.stringAttributes);
                copy.floatAttributes = target.floatAttributes;
                copy.stringAttributes = target.stringAttributes;
                for (auto& [name, value] : sourceFloats)
                    copy.floatAttributes[name] = value;
                for (auto& [name, value] : sourceStrings)
                    copy.stringAttributes[name] = std::move(value);
            }
            result.points().push_back(std::move(copy));
        }
    }
    return result;
}

PointSet filterPointFloatAttribute(const PointSet& input, const std::string& name, float minValue,
                                   float maxValue, bool invert) {
    if (minValue > maxValue) std::swap(minValue, maxValue);
    PointSet output;
    for (const auto& point : input.points()) {
        const auto found   = point.floatAttributes.find(name);
        const bool matches = found != point.floatAttributes.end() && found->second >= minValue &&
                             found->second <= maxValue;
        if (matches != invert) output.points().push_back(point);
    }
    return output;
}

PointSet filterPointStringAttribute(const PointSet& input, const std::string& name,
                                    const std::string& value, bool invert) {
    PointSet output;
    for (const auto& point : input.points()) {
        const auto found   = point.stringAttributes.find(name);
        const bool matches = found != point.stringAttributes.end() && found->second == value;
        if (matches != invert) output.points().push_back(point);
    }
    return output;
}

PointSet densityCullPoints(const PointSet& input, uint32_t seed, float multiplier) {
    PointSet output;
    multiplier = std::max(0.f, multiplier);
    for (size_t i = 0; i < input.points().size(); ++i) {
        const auto&    point      = input.points()[i];
        const uint32_t branchSeed = mix32(seed ^ point.seed ^ uint32_t(i));
        const float    chance     = std::clamp(point.density * multiplier, 0.f, 1.f);
        if (unitFloat(branchSeed) < chance) output.points().push_back(point);
    }
    return output;
}

}  // namespace eve::procgen
