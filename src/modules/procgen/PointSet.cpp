#include "procgen/PointSet.h"

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

}  // namespace eve::procgen
