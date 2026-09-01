#include "procgen/PointSet.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_set>

namespace eve::procgen {
namespace {

uint32_t mix32(uint32_t value) {
    value += 0x9e3779b9u;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}

float unitFloat(uint32_t seed) { return float(mix32(seed) >> 8u) * (1.f / 16777216.f); }

float pointSegmentDistanceSquared(float px, float pz, const ProcgenPoint& a, const ProcgenPoint& b) {
    const float vx      = b.x - a.x;
    const float vz      = b.z - a.z;
    const float length2 = vx * vx + vz * vz;
    const float t       = length2 > 0.f ? std::clamp(((px - a.x) * vx + (pz - a.z) * vz) / length2, 0.f, 1.f) : 0.f;
    const float dx = px - (a.x + vx * t);
    const float dz = pz - (a.z + vz * t);
    return dx * dx + dz * dz;
}

bool pointInPolygon(float x, float z, const std::vector<ProcgenPoint>& polygon) {
    bool inside = false;
    for (size_t i = 0, previous = polygon.size() - 1; i < polygon.size(); previous = i++) {
        const auto& a = polygon[i];
        const auto& b = polygon[previous];
        const bool  crosses = ((a.z > z) != (b.z > z)) && (x < (b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

}  // namespace

int  PointSet::getCount() const { return int(points_.size()); }
bool PointSet::empty() const { return points_.empty(); }
void PointSet::clear() {
    points_.clear();
    attributes_.clear();
}

void appendPointRow(PointSet& output, const PointSet& input, std::size_t index) {
    std::move(output.appendPointFrom(input, index)).expect("PointSet operation requires compatible attribute schemas");
}

void PointSet::reserve(std::size_t count) { points_.reserve(count); }

int PointSet::appendPoint(ProcgenPoint point) {
    points_.push_back(std::move(point));
    const std::size_t row = attributes_.appendRow();
    EV_ASSERT(row + 1 == points_.size(), "PointSet point and attribute rows must remain aligned");
    return int(row);
}

Result<int> PointSet::appendPointFrom(const PointSet& source, std::size_t sourceIndex) {
    if (sourceIndex >= source.points_.size())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "source point index is out of range", "sourceIndex"));
    auto attributeRow = attributes_.appendRowFrom(source.attributes_, sourceIndex);
    if (!attributeRow.ok()) return Result<int>::failure(attributeRow.status());
    try {
        points_.push_back(source.points_[sourceIndex]);
    } catch (...) {
        attributes_.resize(attributeRow.value());
        throw;
    }
    EV_ASSERT(attributeRow.value() + 1 == points_.size(), "PointSet point and attribute rows must remain aligned");
    return Result<int>::success(int(attributeRow.value()));
}

Result<void> PointSet::clearPointAttributes(std::size_t index) {
    if (index >= points_.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.clearRow(index);
}

ProcgenPoint& PointSet::mutablePoint(std::size_t index) {
    EV_PARAM_CHECK(index < points_.size(), "PointSet mutable point index is out of range");
    return points_[index];
}

int PointSet::add(float x, float y, float z) {
    ProcgenPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    return appendPoint(std::move(point));
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

void PointSet::setRotation(int index, float pitch, float yaw, float roll) {
    if (auto* point = pointAt(index)) {
        point->pitch = pitch;
        point->yaw   = yaw;
        point->roll  = roll;
    }
}

float PointSet::getPitch(int index) const {
    const auto* point = pointAt(index);
    return point ? point->pitch : 0.f;
}

float PointSet::getRoll(int index) const {
    const auto* point = pointAt(index);
    return point ? point->roll : 0.f;
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

void PointSet::setBounds(int index, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    if (auto* point = pointAt(index)) {
        point->boundsMinX = std::min(minX, maxX);
        point->boundsMinY = std::min(minY, maxY);
        point->boundsMinZ = std::min(minZ, maxZ);
        point->boundsMaxX = std::max(minX, maxX);
        point->boundsMaxY = std::max(minY, maxY);
        point->boundsMaxZ = std::max(minZ, maxZ);
    }
}

float PointSet::getBoundsMinX(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMinX : 0.f;
}
float PointSet::getBoundsMinY(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMinY : 0.f;
}
float PointSet::getBoundsMinZ(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMinZ : 0.f;
}
float PointSet::getBoundsMaxX(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMaxX : 0.f;
}
float PointSet::getBoundsMaxY(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMaxY : 0.f;
}
float PointSet::getBoundsMaxZ(int index) const {
    const auto* point = pointAt(index);
    return point ? point->boundsMaxZ : 0.f;
}

void PointSet::setColor(int index, float red, float green, float blue, float alpha) {
    if (auto* point = pointAt(index)) {
        point->colorR = std::clamp(red, 0.f, 1.f);
        point->colorG = std::clamp(green, 0.f, 1.f);
        point->colorB = std::clamp(blue, 0.f, 1.f);
        point->colorA = std::clamp(alpha, 0.f, 1.f);
    }
}

float PointSet::getColorR(int index) const {
    const auto* point = pointAt(index);
    return point ? point->colorR : 1.f;
}
float PointSet::getColorG(int index) const {
    const auto* point = pointAt(index);
    return point ? point->colorG : 1.f;
}
float PointSet::getColorB(int index) const {
    const auto* point = pointAt(index);
    return point ? point->colorB : 1.f;
}
float PointSet::getColorA(int index) const {
    const auto* point = pointAt(index);
    return point ? point->colorA : 1.f;
}

void PointSet::setSteepness(int index, float steepness) {
    if (auto* point = pointAt(index)) point->steepness = std::clamp(steepness, 0.f, 1.f);
}

float PointSet::getSteepness(int index) const {
    const auto* point = pointAt(index);
    return point ? point->steepness : 0.f;
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

std::uint64_t PointSet::getPointId(int index) const {
    const auto* point = pointAt(index);
    return point ? point->id : 0;
}

Result<void> PointSet::trySetPointId(int index, std::uint64_t id) {
    auto* point = pointAt(index);
    if (!point)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    if (id == 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point id must be non-zero", "id"));
    for (std::size_t row = 0; row < points_.size(); ++row)
        if (row != std::size_t(index) && points_[row].id == id)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point id is already present", "id"));
    point->id = id;
    return Result<void>::success();
}

Result<void> PointSet::assignPointIds(std::uint64_t namespaceId) {
    if (namespaceId == 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "point id namespace must be non-zero", "namespaceId"));
    std::unordered_set<std::uint64_t> occupied;
    occupied.reserve(points_.size());
    for (const auto& point : points_)
        if (point.id != 0 && !occupied.insert(point.id).second)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "point set contains duplicate ids", "points"));
    PointSet staged = *this;
    for (std::size_t row = 0; row < staged.points_.size(); ++row) {
        if (staged.points_[row].id != 0) continue;
        std::uint64_t candidate = derivePointId(namespaceId, row);
        while (!occupied.insert(candidate).second) candidate = derivePointId(candidate, row + 1);
        staged.points_[row].id = candidate;
    }
    *this = std::move(staged);
    return Result<void>::success();
}

Result<void> PointSet::trySetFloatAttribute(int index, const std::string& name, float value) {
    if (!pointAt(index))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.setFloat(size_t(index), name, value);
}

void PointSet::setFloatAttribute(int index, const std::string& name, float value) {
    trySetFloatAttribute(index, name, value).ignore("compatibility PointSet float attribute setter");
}

float PointSet::getFloatAttribute(int index, const std::string& name, float fallback) const {
    const auto value = index >= 0 ? attributes_.getFloat(size_t(index), name) : std::nullopt;
    return value.value_or(fallback);
}

bool PointSet::hasFloatAttribute(int index, const std::string& name) const {
    return index >= 0 && attributes_.getFloat(size_t(index), name).has_value();
}

Result<void> PointSet::trySetIntAttribute(int index, const std::string& name, std::int64_t value) {
    if (!pointAt(index))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.setInt(size_t(index), name, value);
}

void PointSet::setIntAttribute(int index, const std::string& name, std::int64_t value) {
    trySetIntAttribute(index, name, value).ignore("compatibility PointSet integer attribute setter");
}

std::int64_t PointSet::getIntAttribute(int index, const std::string& name, std::int64_t fallback) const {
    const auto value = index >= 0 ? attributes_.getInt(size_t(index), name) : std::nullopt;
    return value.value_or(fallback);
}

bool PointSet::hasIntAttribute(int index, const std::string& name) const {
    return index >= 0 && attributes_.getInt(size_t(index), name).has_value();
}

Result<void> PointSet::trySetBoolAttribute(int index, const std::string& name, bool value) {
    if (!pointAt(index))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.setBool(size_t(index), name, value);
}

void PointSet::setBoolAttribute(int index, const std::string& name, bool value) {
    trySetBoolAttribute(index, name, value).ignore("compatibility PointSet Boolean attribute setter");
}

bool PointSet::getBoolAttribute(int index, const std::string& name, bool fallback) const {
    const auto value = index >= 0 ? attributes_.getBool(size_t(index), name) : std::nullopt;
    return value.value_or(fallback);
}

bool PointSet::hasBoolAttribute(int index, const std::string& name) const {
    return index >= 0 && attributes_.getBool(size_t(index), name).has_value();
}

Result<void> PointSet::trySetVectorAttribute(int index, const std::string& name, float x, float y, float z) {
    if (!pointAt(index))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.setVector(size_t(index), name, {x, y, z});
}

void PointSet::setVectorAttribute(int index, const std::string& name, float x, float y, float z) {
    trySetVectorAttribute(index, name, x, y, z).ignore("compatibility PointSet vector attribute setter");
}

float PointSet::getVectorAttributeX(int index, const std::string& name, float fallback) const {
    const auto value = index >= 0 ? attributes_.getVector(size_t(index), name) : std::nullopt;
    return value ? value->x : fallback;
}

float PointSet::getVectorAttributeY(int index, const std::string& name, float fallback) const {
    const auto value = index >= 0 ? attributes_.getVector(size_t(index), name) : std::nullopt;
    return value ? value->y : fallback;
}

float PointSet::getVectorAttributeZ(int index, const std::string& name, float fallback) const {
    const auto value = index >= 0 ? attributes_.getVector(size_t(index), name) : std::nullopt;
    return value ? value->z : fallback;
}

bool PointSet::hasVectorAttribute(int index, const std::string& name) const {
    return index >= 0 && attributes_.getVector(size_t(index), name).has_value();
}

Result<void> PointSet::trySetStringAttribute(int index, const std::string& name, const std::string& value) {
    if (!pointAt(index))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "point index is out of range", "index"));
    return attributes_.setString(size_t(index), name, value);
}

void PointSet::setStringAttribute(int index, const std::string& name, const std::string& value) {
    trySetStringAttribute(index, name, value).ignore("compatibility PointSet string attribute setter");
}

std::string PointSet::getStringAttribute(int index, const std::string& name, const std::string& fallback) const {
    const auto value = index >= 0 ? attributes_.getString(size_t(index), name) : std::nullopt;
    return value ? std::string(*value) : fallback;
}

bool PointSet::hasStringAttribute(int index, const std::string& name) const {
    return index >= 0 && attributes_.getString(size_t(index), name).has_value();
}

std::string PointSet::getAttributeType(int index, const std::string& name) const {
    if (!pointAt(index) || !attributes_.has(size_t(index), name)) return {};
    const auto type = attributes_.typeOf(name);
    return type ? std::string(procgenAttributeTypeName(*type)) : std::string{};
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

std::uint64_t derivePointId(std::uint64_t namespaceId, std::uint64_t ordinal) {
    std::uint64_t value = namespaceId ^ (ordinal + 0x9e3779b97f4a7c15ull + (namespaceId << 6u) +
                                         (namespaceId >> 2u));
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return value == 0 ? 1 : value;
}

PointSet sampleGridPoints(int width, int depth, float spacing, uint32_t seed, float jitter) {
    PointSet output;
    if (width <= 0 || depth <= 0 || spacing <= 0.f) return output;
    jitter = std::clamp(jitter, 0.f, 1.f);
    output.reserve(size_t(width) * size_t(depth));
    const float extent = spacing * jitter * 0.5f;
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            const uint32_t pointSeed = mix32(seed ^ uint32_t(z * width + x));
            ProcgenPoint   point;
            point.x    = float(x) * spacing + (unitFloat(pointSeed) * 2.f - 1.f) * extent;
            point.z    = float(z) * spacing + (unitFloat(pointSeed ^ 0xa511e9b3u) * 2.f - 1.f) * extent;
            point.seed = pointSeed ? pointSeed : 1u;
            point.id   = derivePointId((std::uint64_t(seed) << 32u) | 0x47524944u,
                                       std::uint64_t(z) * std::uint64_t(width) + std::uint64_t(x));
            const int appended = output.appendPoint(std::move(point));
            (void)appended;
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
    const auto inBounds = [&](float x, float y) {
        return x >= 0.f && x <= float(width) && y >= 0.f && y <= float(depth);
    };
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

    output.reserve(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) {
        ProcgenPoint point;
        point.x    = xs[i];
        point.y    = 0.f;
        point.z    = ys[i];
        point.seed = mix32(seed ^ uint32_t(i));
        if (point.seed == 0) point.seed = 1;
        point.id = derivePointId((std::uint64_t(seed) << 32u) | 0x504f4953u, i);
        const int appended = output.appendPoint(std::move(point));
        (void)appended;
    }
    return output;
}

PointSet filterPointHeight(const PointSet& input, float minHeight, float maxHeight) {
    PointSet output;
    if (minHeight > maxHeight) std::swap(minHeight, maxHeight);
    for (size_t index = 0; index < input.points().size(); ++index)
        if (input.points()[index].y >= minHeight && input.points()[index].y <= maxHeight)
            appendPointRow(output, input, index);
    return output;
}

PointSet filterPointDensity(const PointSet& input, float minDensity, float maxDensity) {
    PointSet output;
    if (minDensity > maxDensity) std::swap(minDensity, maxDensity);
    for (size_t index = 0; index < input.points().size(); ++index)
        if (input.points()[index].density >= minDensity && input.points()[index].density <= maxDensity)
            appendPointRow(output, input, index);
    return output;
}

PointSet filterPointBox(const PointSet& input, float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                        bool invert) {
    if (minX > maxX) std::swap(minX, maxX);
    if (minY > maxY) std::swap(minY, maxY);
    if (minZ > maxZ) std::swap(minZ, maxZ);
    PointSet output;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& point  = input.points()[index];
        const bool  inside = point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY &&
                             point.z >= minZ && point.z <= maxZ;
        if (inside != invert) appendPointRow(output, input, index);
    }
    return output;
}

PointSet filterPointSlope(const PointSet& input, float minDegrees, float maxDegrees) {
    if (minDegrees > maxDegrees) std::swap(minDegrees, maxDegrees);
    minDegrees = std::clamp(minDegrees, 0.f, 180.f);
    maxDegrees = std::clamp(maxDegrees, 0.f, 180.f);
    constexpr float radiansToDegrees = 57.29577951308232f;
    PointSet        output;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& point = input.points()[index];
        const float length =
            std::sqrt(point.normalX * point.normalX + point.normalY * point.normalY + point.normalZ * point.normalZ);
        const float up     = length > 0.f ? std::clamp(point.normalY / length, -1.f, 1.f) : 1.f;
        const float slope  = std::acos(up) * radiansToDegrees;
        if (slope >= minDegrees && slope <= maxDegrees) appendPointRow(output, input, index);
    }
    return output;
}

PointSet filterPointsByPolygon(const PointSet& input, const PointSet& polygon, bool invert) {
    PointSet output;
    if (polygon.points().size() < 3) return output;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& point  = input.points()[index];
        const bool inside = pointInPolygon(point.x, point.z, polygon.points());
        if (inside != invert) appendPointRow(output, input, index);
    }
    return output;
}

PointSet filterPointsBySplineDistance(const PointSet& input, const PointSet& controlPoints, float minDistance,
                                      float maxDistance) {
    PointSet output;
    if (controlPoints.points().size() < 2) return output;
    if (minDistance > maxDistance) std::swap(minDistance, maxDistance);
    minDistance = std::max(0.f, minDistance);
    maxDistance = std::max(0.f, maxDistance);
    const float minSquared = minDistance * minDistance;
    const float maxSquared = maxDistance * maxDistance;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& point   = input.points()[index];
        float nearest = std::numeric_limits<float>::max();
        for (size_t i = 1; i < controlPoints.points().size(); ++i) {
            nearest = std::min(nearest, pointSegmentDistanceSquared(point.x, point.z, controlPoints.points()[i - 1],
                                            controlPoints.points()[i]));
        }
        if (nearest >= minSquared && nearest <= maxSquared) appendPointRow(output, input, index);
    }
    return output;
}

PointSet excludePointRadius(const PointSet& input, float x, float z, float radius) {
    PointSet    output;
    const float radiusSquared = std::max(0.f, radius) * std::max(0.f, radius);
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& point = input.points()[index];
        const float dx = point.x - x;
        const float dz = point.z - z;
        if (dx * dx + dz * dz > radiusSquared) appendPointRow(output, input, index);
    }
    return output;
}

PointSet jitterPointPositions(const PointSet& input, uint32_t seed, float amountX, float amountZ) {
    PointSet output = input;
    amountX         = std::max(0.f, amountX);
    amountZ         = std::max(0.f, amountZ);
    for (size_t i = 0; i < output.points().size(); ++i) {
        auto&          point      = output.mutablePoint(i);
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
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto& candidate = input.points()[index];
        bool keep = true;
        for (const auto& accepted : output.points()) {
            const float dx = candidate.x - accepted.x;
            const float dz = candidate.z - accepted.z;
            if (dx * dx + dz * dz < radiusSquared) {
                keep = false;
                break;
            }
        }
        if (keep) appendPointRow(output, input, index);
    }
    return output;
}

PointSet samplePolylinePoints(const PointSet& controlPoints, float spacing, uint32_t seed, float lateralJitter) {
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
            point.id   = derivePointId((std::uint64_t(seed) << 32u) | 0x504c494eu, sampleIndex);
            const int appended = output.appendPoint(std::move(point));
            (void)appended;
            ++sampleIndex;
            distanceToNext += spacing;
        }
        distanceToNext -= length;
    }
    return output;
}

PointSet mergePointSets(const PointSet& first, const PointSet& second) {
    PointSet output;
    output.reserve(first.points().size() + second.points().size());
    for (size_t index = 0; index < first.points().size(); ++index) appendPointRow(output, first, index);
    for (size_t index = 0; index < second.points().size(); ++index) appendPointRow(output, second, index);
    return output;
}

namespace {

struct PointIdentity {
    std::uint64_t id   = 0;
    std::uint32_t seed = 0;
    float         x = 0.f, y = 0.f, z = 0.f;

    bool operator==(const PointIdentity& other) const noexcept {
        if (id != 0 || other.id != 0) return id != 0 && id == other.id;
        return seed == other.seed && x == other.x && y == other.y && z == other.z;
    }
};

struct PointIdentityHash {
    std::size_t operator()(const PointIdentity& value) const noexcept {
        if (value.id != 0) return std::hash<std::uint64_t>{}(value.id);
        auto combine = [](std::size_t hash, std::uint32_t part) {
            return hash ^ (std::size_t(part) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u));
        };
        std::size_t hash = value.seed;
        hash = combine(hash, value.x == 0.f ? 0u : std::bit_cast<std::uint32_t>(value.x));
        hash = combine(hash, value.y == 0.f ? 0u : std::bit_cast<std::uint32_t>(value.y));
        return combine(hash, value.z == 0.f ? 0u : std::bit_cast<std::uint32_t>(value.z));
    }
};

PointIdentity identityOf(const ProcgenPoint& point) {
    return {point.id, point.seed, point.x, point.y, point.z};
}

void rotateEuler(float& x, float& y, float& z, float pitchDegrees, float yawDegrees, float rollDegrees) {
    constexpr float degreesToRadians = 0.017453292519943295f;
    const float pitch = pitchDegrees * degreesToRadians;
    const float yaw = yawDegrees * degreesToRadians;
    const float roll = rollDegrees * degreesToRadians;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const float rolledX = x * cr - y * sr;
    const float rolledY = x * sr + y * cr;
    const float pitchedY = rolledY * cp - z * sp;
    const float pitchedZ = rolledY * sp + z * cp;
    x = rolledX * cy - pitchedZ * sy;
    y = pitchedY;
    z = rolledX * sy + pitchedZ * cy;
}

}  // namespace

PointSet unionPointSets(const PointSet& first, const PointSet& second) {
    PointSet result;
    std::unordered_set<PointIdentity, PointIdentityHash> identities;
    identities.reserve(first.points().size() + second.points().size());
    result.reserve(first.points().size() + second.points().size());
    for (size_t index = 0; index < first.points().size(); ++index)
        if (identities.insert(identityOf(first.points()[index])).second) appendPointRow(result, first, index);
    for (size_t index = 0; index < second.points().size(); ++index)
        if (identities.insert(identityOf(second.points()[index])).second) appendPointRow(result, second, index);
    return result;
}

PointSet intersectPointSets(const PointSet& first, const PointSet& second) {
    PointSet result;
    std::unordered_set<PointIdentity, PointIdentityHash> identities;
    identities.reserve(second.points().size());
    for (const auto& point : second.points()) identities.insert(identityOf(point));
    result.reserve(std::min(first.points().size(), second.points().size()));
    for (size_t index = 0; index < first.points().size(); ++index)
        if (identities.contains(identityOf(first.points()[index]))) appendPointRow(result, first, index);
    return result;
}

PointSet differencePointSets(const PointSet& first, const PointSet& second) {
    PointSet result;
    std::unordered_set<PointIdentity, PointIdentityHash> identities;
    identities.reserve(second.points().size());
    for (const auto& point : second.points()) identities.insert(identityOf(point));
    result.reserve(first.points().size());
    for (size_t index = 0; index < first.points().size(); ++index)
        if (!identities.contains(identityOf(first.points()[index]))) appendPointRow(result, first, index);
    return result;
}

PointSet transformPointSet(const PointSet& input, float translateX, float translateY, float translateZ,
                           float yawDegrees, float scaleX, float scaleY, float scaleZ) {
    return transformPointSet3D(input, translateX, translateY, translateZ, 0.f, yawDegrees, 0.f, scaleX, scaleY, scaleZ);
}

PointSet transformPointSet3D(const PointSet& input, float translateX, float translateY, float translateZ,
                             float pitchDegrees, float yawDegrees, float rollDegrees, float scaleX, float scaleY,
                             float scaleZ) {
    PointSet output = input;
    for (size_t index = 0; index < output.points().size(); ++index) {
        auto& point = output.mutablePoint(index);
        float x = point.x * scaleX;
        float y = point.y * scaleY;
        float z = point.z * scaleZ;
        rotateEuler(x, y, z, pitchDegrees, yawDegrees, rollDegrees);
        point.x = x + translateX;
        point.y = y + translateY;
        point.z = z + translateZ;
        point.pitch += pitchDegrees;
        point.yaw += yawDegrees;
        point.roll += rollDegrees;
        point.scaleX *= scaleX;
        point.scaleY *= scaleY;
        point.scaleZ *= scaleZ;

        point.normalX = point.normalX / (std::abs(scaleX) > 0.000001f ? scaleX : 1.f);
        point.normalY = point.normalY / (std::abs(scaleY) > 0.000001f ? scaleY : 1.f);
        point.normalZ = point.normalZ / (std::abs(scaleZ) > 0.000001f ? scaleZ : 1.f);
        rotateEuler(point.normalX, point.normalY, point.normalZ, pitchDegrees, yawDegrees, rollDegrees);
        const float normalLength =
            std::sqrt(point.normalX * point.normalX + point.normalY * point.normalY + point.normalZ * point.normalZ);
        if (normalLength > 0.f) {
            point.normalX /= normalLength;
            point.normalY /= normalLength;
            point.normalZ /= normalLength;
        }
    }
    return output;
}

PointSet copyPointsToTargets(const PointSet& source, const PointSet& targets, bool inheritTargetAttributes) {
    constexpr float degreesToRadians = 0.017453292519943295f;
    PointSet result;
    if (!targets.points().empty() &&
        source.points().size() > std::numeric_limits<size_t>::max() / targets.points().size())
        return result;
    result.reserve(source.points().size() * targets.points().size());
    for (size_t targetIndex = 0; targetIndex < targets.points().size(); ++targetIndex) {
        const auto& target  = targets.points()[targetIndex];
        const float radians = target.yaw * degreesToRadians;
        const float cosine  = std::cos(radians);
        const float sine    = std::sin(radians);
        for (size_t sourceIndex = 0; sourceIndex < source.points().size(); ++sourceIndex) {
            const auto&  sourcePoint = source.points()[sourceIndex];
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
            const std::uint64_t targetIdentity = target.id != 0 ? target.id : std::uint64_t(target.seed);
            const std::uint64_t sourceIdentity = sourcePoint.id != 0 ? sourcePoint.id : std::uint64_t(sourcePoint.seed);
            copy.id = derivePointId(targetIdentity, sourceIdentity);
            const int resultIndex = result.appendPoint(std::move(copy));
            if (inheritTargetAttributes) {
                for (size_t column = 0; column < targets.attributes().columnCount(); ++column) {
                    const std::string name(targets.attributes().columnName(column));
                    if (!targets.attributes().has(targetIndex, name)) continue;
                    switch (*targets.attributes().typeOf(name)) {
                        case ProcgenAttributeType::Float:
                            result
                                .trySetFloatAttribute(resultIndex, name,
                                                      *targets.attributes().getFloat(targetIndex, name))
                                .expect("copy target float attribute");
                            break;
                        case ProcgenAttributeType::Int:
                            result
                                .trySetIntAttribute(resultIndex, name, *targets.attributes().getInt(targetIndex, name))
                                .expect("copy target integer attribute");
                            break;
                        case ProcgenAttributeType::Bool:
                            result
                                .trySetBoolAttribute(resultIndex, name,
                                                     *targets.attributes().getBool(targetIndex, name))
                                .expect("copy target Boolean attribute");
                            break;
                        case ProcgenAttributeType::Vector: {
                            const auto value = *targets.attributes().getVector(targetIndex, name);
                            result.trySetVectorAttribute(resultIndex, name, value.x, value.y, value.z)
                                .expect("copy target vector attribute");
                            break;
                        }
                        case ProcgenAttributeType::String:
                            result
                                .trySetStringAttribute(resultIndex, name,
                                                       std::string(*targets.attributes().getString(targetIndex, name)))
                                .expect("copy target string attribute");
                            break;
                    }
                }
            }
            for (size_t column = 0; column < source.attributes().columnCount(); ++column) {
                const std::string name(source.attributes().columnName(column));
                if (!source.attributes().has(sourceIndex, name)) continue;
                switch (*source.attributes().typeOf(name)) {
                    case ProcgenAttributeType::Float:
                        result.trySetFloatAttribute(resultIndex, name, *source.attributes().getFloat(sourceIndex, name))
                            .expect("copy source float attribute");
                        break;
                    case ProcgenAttributeType::Int:
                        result.trySetIntAttribute(resultIndex, name, *source.attributes().getInt(sourceIndex, name))
                            .expect("copy source integer attribute");
                        break;
                    case ProcgenAttributeType::Bool:
                        result.trySetBoolAttribute(resultIndex, name, *source.attributes().getBool(sourceIndex, name))
                            .expect("copy source Boolean attribute");
                        break;
                    case ProcgenAttributeType::Vector: {
                        const auto value = *source.attributes().getVector(sourceIndex, name);
                        result.trySetVectorAttribute(resultIndex, name, value.x, value.y, value.z)
                            .expect("copy source vector attribute");
                        break;
                    }
                    case ProcgenAttributeType::String:
                        result
                            .trySetStringAttribute(resultIndex, name,
                                                   std::string(*source.attributes().getString(sourceIndex, name)))
                            .expect("copy source string attribute");
                        break;
                }
            }
        }
    }
    return result;
}

PointSet remapPointDensity(const PointSet& input, float inputMin, float inputMax, float outputMin, float outputMax,
                           bool clampOutput) {
    PointSet result = input;
    const float inputRange = inputMax - inputMin;
    for (size_t index = 0; index < result.points().size(); ++index) {
        auto& point      = result.mutablePoint(index);
        float normalized = (point.density - inputMin) / inputRange;
        if (clampOutput) normalized = std::clamp(normalized, 0.f, 1.f);
        point.density = outputMin + normalized * (outputMax - outputMin);
    }
    return result;
}

PointSet mathPointFloatAttribute(const PointSet& input, const std::string& attribute,
                                 const std::string& outputAttribute, const std::string& operation, float operand,
                                 float defaultValue) {
    PointSet result = input;
    for (size_t index = 0; index < result.points().size(); ++index) {
        const float value  = result.getFloatAttribute(int(index), attribute, defaultValue);
        float output = value;
        if (operation == "add")
            output += operand;
        else if (operation == "subtract")
            output -= operand;
        else if (operation == "multiply")
            output *= operand;
        else if (operation == "divide")
            output /= operand;
        else if (operation == "min")
            output = std::min(output, operand);
        else if (operation == "max")
            output = std::max(output, operand);
        result.trySetFloatAttribute(int(index), outputAttribute, output)
            .expect("mathPointFloatAttribute output schema");
    }
    return result;
}

PointSet filterPointFloatAttribute(const PointSet& input, const std::string& name, float minValue, float maxValue,
                                   bool invert) {
    if (minValue > maxValue) std::swap(minValue, maxValue);
    PointSet output;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto value   = input.attributes().getFloat(index, name);
        const bool matches = value && *value >= minValue && *value <= maxValue;
        if (matches != invert) appendPointRow(output, input, index);
    }
    return output;
}

PointSet filterPointStringAttribute(const PointSet& input, const std::string& name, const std::string& value,
                                    bool invert) {
    PointSet output;
    for (size_t index = 0; index < input.points().size(); ++index) {
        const auto found   = input.attributes().getString(index, name);
        const bool matches = found && *found == value;
        if (matches != invert) appendPointRow(output, input, index);
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
        if (unitFloat(branchSeed) < chance) appendPointRow(output, input, i);
    }
    return output;
}

}  // namespace eve::procgen
