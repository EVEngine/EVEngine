#include "procgen/SpatialData.h"

#include "procgen/MeshBuild.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::procgen {
namespace {

uint32_t mixSpatialSeed(uint32_t value) {
    value += 0x9e3779b9u;
    value = (value ^ (value >> 16u)) * 0x21f0aaadu;
    value = (value ^ (value >> 15u)) * 0x735a2d97u;
    return value ^ (value >> 15u);
}

float unitFloat(uint32_t seed) { return float(mixSpatialSeed(seed) >> 8u) * (1.f / 16777216.f); }

SpatialBounds unionBounds(const SpatialBounds& a, const SpatialBounds& b) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    return {std::min(a.minX, b.minX), std::min(a.minY, b.minY), std::min(a.minZ, b.minZ),
            std::max(a.maxX, b.maxX), std::max(a.maxY, b.maxY), std::max(a.maxZ, b.maxZ), true};
}

SpatialBounds intersectionBounds(const SpatialBounds& a, const SpatialBounds& b) {
    if (!a.valid || !b.valid) return {};
    SpatialBounds result{std::max(a.minX, b.minX), std::max(a.minY, b.minY),
                         std::max(a.minZ, b.minZ), std::min(a.maxX, b.maxX),
                         std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ), true};
    if (result.minX > result.maxX || result.minY > result.maxY || result.minZ > result.maxZ)
        return {};
    return result;
}

float pointSegmentDistanceSquared(float px, float py, float pz, const ProcgenPoint& a,
                                  const ProcgenPoint& b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float vz = b.z - a.z;
    const float l2 = vx * vx + vy * vy + vz * vz;
    const float t  = l2 > 0.f ? std::clamp(((px - a.x) * vx + (py - a.y) * vy +
                                            (pz - a.z) * vz) /
                                               l2,
                                           0.f, 1.f)
                              : 0.f;
    const float dx = px - (a.x + vx * t);
    const float dy = py - (a.y + vy * t);
    const float dz = pz - (a.z + vz * t);
    return dx * dx + dy * dy + dz * dz;
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

bool triangleHeightAtXZ(const std::vector<float>& positions, std::uint32_t ia, std::uint32_t ib,
                        std::uint32_t ic, float x, float z, float& y, float& nx, float& ny,
                        float& nz) {
    const size_t a = size_t(ia) * 3;
    const size_t b = size_t(ib) * 3;
    const size_t c = size_t(ic) * 3;
    if (a + 2 >= positions.size() || b + 2 >= positions.size() || c + 2 >= positions.size())
        return false;
    const float ax = positions[a], ay = positions[a + 1], az = positions[a + 2];
    const float bx = positions[b], by = positions[b + 1], bz = positions[b + 2];
    const float cx = positions[c], cy = positions[c + 1], cz = positions[c + 2];
    const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    if (std::abs(denominator) <= 1e-8f) return false;
    const float wa = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator;
    const float wb = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator;
    const float wc = 1.f - wa - wb;
    constexpr float epsilon = 1e-5f;
    if (wa < -epsilon || wb < -epsilon || wc < -epsilon) return false;
    y = wa * ay + wb * by + wc * cy;
    const float ux = bx - ax, uy = by - ay, uz = bz - az;
    const float vx = cx - ax, vy = cy - ay, vz = cz - az;
    nx = uy * vz - uz * vy;
    ny = uz * vx - ux * vz;
    nz = ux * vy - uy * vx;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 0.f) {
        nx /= length;
        ny /= length;
        nz /= length;
    }
    if (ny < 0.f) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    return true;
}

bool closestMeshHeight(const MeshBuild& mesh, float x, float z, float referenceY, float& y,
                       float& nx, float& ny, float& nz) {
    bool found = false;
    float bestDistance = std::numeric_limits<float>::max();
    const auto& positions = mesh.positions();
    const auto& indices = mesh.indices();
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        float candidateY = 0.f, candidateNx = 0.f, candidateNy = 1.f, candidateNz = 0.f;
        if (!triangleHeightAtXZ(positions, indices[i], indices[i + 1], indices[i + 2], x, z,
                               candidateY, candidateNx, candidateNy, candidateNz))
            continue;
        const float distance = std::abs(referenceY - candidateY);
        if (distance >= bestDistance) continue;
        found = true;
        bestDistance = distance;
        y = candidateY;
        nx = candidateNx;
        ny = candidateNy;
        nz = candidateNz;
    }
    return found;
}

}  // namespace

SpatialData SpatialData::fromPoints(const PointSet& points) {
    SpatialData result;
    if (points.empty()) return result;
    result.kind_   = Kind::Points;
    result.points_ = points;
    auto& bounds   = result.bounds_;
    bounds.minX = bounds.minY = bounds.minZ = std::numeric_limits<float>::max();
    bounds.maxX = bounds.maxY = bounds.maxZ = std::numeric_limits<float>::lowest();
    for (const auto& point : points.points()) {
        bounds.minX = std::min(bounds.minX, point.x);
        bounds.minY = std::min(bounds.minY, point.y);
        bounds.minZ = std::min(bounds.minZ, point.z);
        bounds.maxX = std::max(bounds.maxX, point.x);
        bounds.maxY = std::max(bounds.maxY, point.y);
        bounds.maxZ = std::max(bounds.maxZ, point.z);
    }
    bounds.valid = true;
    return result;
}

SpatialData SpatialData::box(float minX, float minY, float minZ, float maxX, float maxY,
                             float maxZ) {
    SpatialData result;
    result.kind_   = Kind::Box;
    result.bounds_ = {std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ),
                      std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ), true};
    return result;
}

SpatialData SpatialData::sphere(float x, float y, float z, float radius) {
    SpatialData result;
    if (radius <= 0.f) return result;
    result.kind_    = Kind::Sphere;
    result.centerX_ = x;
    result.centerY_ = y;
    result.centerZ_ = z;
    result.radius_  = radius;
    result.bounds_  = {x - radius, y - radius, z - radius, x + radius, y + radius, z + radius,
                       true};
    return result;
}

SpatialData SpatialData::polygon(const PointSet& controlPoints, float minY, float maxY) {
    SpatialData result = fromPoints(controlPoints);
    if (controlPoints.getCount() < 3) return {};
    result.kind_ = Kind::Polygon;
    result.bounds_.minY = std::min(minY, maxY);
    result.bounds_.maxY = std::max(minY, maxY);
    return result;
}

SpatialData SpatialData::spline(const PointSet& controlPoints, float radius) {
    SpatialData result = fromPoints(controlPoints);
    if (controlPoints.getCount() < 2 || radius < 0.f) return {};
    result.kind_   = Kind::Spline;
    result.radius_ = radius;
    result.bounds_.minX -= radius;
    result.bounds_.minY -= radius;
    result.bounds_.minZ -= radius;
    result.bounds_.maxX += radius;
    result.bounds_.maxY += radius;
    result.bounds_.maxZ += radius;
    return result;
}

SpatialData SpatialData::heightfield(const Heightmap& heightmap, float originX, float originZ,
                                     float cellSize, float heightScale) {
    SpatialData result;
    if (heightmap.getWidth() <= 0 || heightmap.getHeight() <= 0 || cellSize <= 0.f) return result;
    result.kind_        = Kind::Heightfield;
    result.heightmap_   = std::make_shared<Heightmap>(heightmap);
    result.originX_     = originX;
    result.originZ_     = originZ;
    result.cellSize_    = cellSize;
    result.heightScale_ = heightScale;
    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = std::numeric_limits<float>::lowest();
    for (int z = 0; z < heightmap.getHeight(); ++z) {
        for (int x = 0; x < heightmap.getWidth(); ++x) {
            const float value = heightmap.height(x, z) * heightScale;
            minHeight         = std::min(minHeight, value);
            maxHeight         = std::max(maxHeight, value);
        }
    }
    result.bounds_ = {originX, minHeight, originZ,
                      originX + float(heightmap.getWidth() - 1) * cellSize, maxHeight,
                      originZ + float(heightmap.getHeight() - 1) * cellSize, true};
    return result;
}

SpatialData SpatialData::textureMask(const Heightmap& values, float originX, float originZ,
                                     float cellSize, float minValue, float maxValue, float minY,
                                     float maxY) {
    SpatialData result;
    if (values.getWidth() <= 0 || values.getHeight() <= 0 || cellSize <= 0.f) return result;
    result.kind_ = Kind::TextureMask;
    result.heightmap_ = std::make_shared<Heightmap>(values);
    result.originX_ = originX;
    result.originZ_ = originZ;
    result.cellSize_ = cellSize;
    result.minValue_ = std::min(minValue, maxValue);
    result.maxValue_ = std::max(minValue, maxValue);
    result.bounds_ = {originX, std::min(minY, maxY), originZ,
                      originX + float(values.getWidth() - 1) * cellSize,
                      std::max(minY, maxY),
                      originZ + float(values.getHeight() - 1) * cellSize, true};
    return result;
}

SpatialData SpatialData::meshSurface(const MeshBuild& mesh, float tolerance) {
    SpatialData result;
    if (mesh.positions().size() < 9 || mesh.indices().size() < 3 || tolerance < 0.f)
        return result;
    result.kind_ = Kind::MeshSurface;
    result.mesh_ = std::make_shared<MeshBuild>(mesh);
    result.radius_ = tolerance;
    auto& bounds = result.bounds_;
    bounds.minX = bounds.minY = bounds.minZ = std::numeric_limits<float>::max();
    bounds.maxX = bounds.maxY = bounds.maxZ = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i + 2 < mesh.positions().size(); i += 3) {
        bounds.minX = std::min(bounds.minX, mesh.positions()[i]);
        bounds.minY = std::min(bounds.minY, mesh.positions()[i + 1]);
        bounds.minZ = std::min(bounds.minZ, mesh.positions()[i + 2]);
        bounds.maxX = std::max(bounds.maxX, mesh.positions()[i]);
        bounds.maxY = std::max(bounds.maxY, mesh.positions()[i + 1]);
        bounds.maxZ = std::max(bounds.maxZ, mesh.positions()[i + 2]);
    }
    bounds.minY -= tolerance;
    bounds.maxY += tolerance;
    bounds.valid = true;
    return result;
}

SpatialData SpatialData::unite(const SpatialData& a, const SpatialData& b) {
    SpatialData result;
    result.kind_   = Kind::Union;
    result.left_   = std::make_shared<SpatialData>(a);
    result.right_  = std::make_shared<SpatialData>(b);
    result.bounds_ = unionBounds(a.bounds(), b.bounds());
    return result;
}

SpatialData SpatialData::intersect(const SpatialData& a, const SpatialData& b) {
    SpatialData result;
    result.kind_   = Kind::Intersection;
    result.left_   = std::make_shared<SpatialData>(a);
    result.right_  = std::make_shared<SpatialData>(b);
    result.bounds_ = intersectionBounds(a.bounds(), b.bounds());
    return result;
}

SpatialData SpatialData::subtract(const SpatialData& a, const SpatialData& b) {
    SpatialData result;
    result.kind_   = Kind::Difference;
    result.left_   = std::make_shared<SpatialData>(a);
    result.right_  = std::make_shared<SpatialData>(b);
    result.bounds_ = a.bounds();
    return result;
}

std::string SpatialData::getKind() const {
    switch (kind_) {
        case Kind::Points: return "points";
        case Kind::Box: return "volume.box";
        case Kind::Sphere: return "volume.sphere";
        case Kind::Polygon: return "volume.polygon";
        case Kind::Spline: return "spline";
        case Kind::Heightfield: return "surface.heightfield";
        case Kind::TextureMask: return "volume.texture_mask";
        case Kind::MeshSurface: return "surface.mesh";
        case Kind::Union: return "union";
        case Kind::Intersection: return "intersection";
        case Kind::Difference: return "difference";
        default: return "empty";
    }
}

bool SpatialData::contains(float x, float y, float z) const {
    switch (kind_) {
        case Kind::Points:
            for (const auto& point : points_.points())
                if (point.x == x && point.y == y && point.z == z) return true;
            return false;
        case Kind::Box:
            return x >= bounds_.minX && x <= bounds_.maxX && y >= bounds_.minY &&
                   y <= bounds_.maxY && z >= bounds_.minZ && z <= bounds_.maxZ;
        case Kind::Sphere: {
            const float dx = x - centerX_;
            const float dy = y - centerY_;
            const float dz = z - centerZ_;
            return dx * dx + dy * dy + dz * dz <= radius_ * radius_;
        }
        case Kind::Polygon:
            return y >= bounds_.minY && y <= bounds_.maxY &&
                   pointInPolygon(x, z, points_.points());
        case Kind::Spline:
            for (size_t i = 1; i < points_.points().size(); ++i)
                if (pointSegmentDistanceSquared(x, y, z, points_.points()[i - 1],
                                                points_.points()[i]) <= radius_ * radius_)
                    return true;
            return false;
        case Kind::Heightfield: {
            if (x < bounds_.minX || x > bounds_.maxX || z < bounds_.minZ || z > bounds_.maxZ)
                return false;
            const float hx      = (x - originX_) / cellSize_;
            const float hz      = (z - originZ_) / cellSize_;
            const float surface = heightmap_->sampleBilinear(hx, hz) * heightScale_;
            return std::abs(y - surface) <= cellSize_ * 0.5f;
        }
        case Kind::TextureMask: {
            if (x < bounds_.minX || x > bounds_.maxX || y < bounds_.minY || y > bounds_.maxY ||
                z < bounds_.minZ || z > bounds_.maxZ)
                return false;
            const float value = heightmap_->sampleBilinear((x - originX_) / cellSize_,
                                                            (z - originZ_) / cellSize_);
            return value >= minValue_ && value <= maxValue_;
        }
        case Kind::MeshSurface: {
            float surfaceY = 0.f, nx = 0.f, ny = 1.f, nz = 0.f;
            return closestMeshHeight(*mesh_, x, z, y, surfaceY, nx, ny, nz) &&
                   std::abs(y - surfaceY) <= radius_;
        }
        case Kind::Union: return left_->contains(x, y, z) || right_->contains(x, y, z);
        case Kind::Intersection: return left_->contains(x, y, z) && right_->contains(x, y, z);
        case Kind::Difference: return left_->contains(x, y, z) && !right_->contains(x, y, z);
        default: return false;
    }
}

SpatialBounds SpatialData::bounds() const { return bounds_; }
bool          SpatialData::hasBounds() const { return bounds_.valid; }
float         SpatialData::getMinX() const { return bounds_.valid ? bounds_.minX : 0.f; }
float         SpatialData::getMinY() const { return bounds_.valid ? bounds_.minY : 0.f; }
float         SpatialData::getMinZ() const { return bounds_.valid ? bounds_.minZ : 0.f; }
float         SpatialData::getMaxX() const { return bounds_.valid ? bounds_.maxX : 0.f; }
float         SpatialData::getMaxY() const { return bounds_.valid ? bounds_.maxY : 0.f; }
float         SpatialData::getMaxZ() const { return bounds_.valid ? bounds_.maxZ : 0.f; }

PointSet SpatialData::sample(float spacing, uint32_t seed, float jitter) const {
    PointSet output;
    if (!bounds_.valid || spacing <= 0.f) return output;
    jitter            = std::clamp(jitter, 0.f, 1.f);
    const float extent = spacing * jitter * 0.5f;
    uint32_t    index  = 0;
    if (kind_ == Kind::Heightfield || kind_ == Kind::MeshSurface) {
        for (float z = bounds_.minZ; z <= bounds_.maxZ + spacing * 0.001f; z += spacing) {
            for (float x = bounds_.minX; x <= bounds_.maxX + spacing * 0.001f; x += spacing) {
                const uint32_t pointSeed = mixSpatialSeed(seed ^ index++);
                const float sx = std::clamp(x + (unitFloat(pointSeed) * 2.f - 1.f) * extent,
                                            bounds_.minX, bounds_.maxX);
                const float sz = std::clamp(
                    z + (unitFloat(pointSeed ^ 0x02e5be93u) * 2.f - 1.f) * extent,
                    bounds_.minZ, bounds_.maxZ);
                const int added = output.add(sx, 0.f, sz);
                output.setPointSeed(added, pointSeed);
            }
        }
        return project(output);
    }
    for (float z = bounds_.minZ; z <= bounds_.maxZ + spacing * 0.001f; z += spacing) {
        for (float y = bounds_.minY; y <= bounds_.maxY + spacing * 0.001f; y += spacing) {
            for (float x = bounds_.minX; x <= bounds_.maxX + spacing * 0.001f; x += spacing) {
                const uint32_t pointSeed = mixSpatialSeed(seed ^ index++);
                const float sx = bounds_.maxX > bounds_.minX
                                     ? x + (unitFloat(pointSeed) * 2.f - 1.f) * extent
                                     : x;
                const float sy = bounds_.maxY > bounds_.minY
                                     ? y + (unitFloat(pointSeed ^ 0x68bc21ebu) * 2.f - 1.f) * extent
                                     : y;
                const float sz = bounds_.maxZ > bounds_.minZ
                                     ? z + (unitFloat(pointSeed ^ 0x02e5be93u) * 2.f - 1.f) * extent
                                     : z;
                if (!contains(sx, sy, sz)) continue;
                const int added = output.add(sx, sy, sz);
                output.setPointSeed(added, pointSeed);
            }
        }
    }
    return output;
}

PointSet SpatialData::filter(const PointSet& input, bool invert) const {
    PointSet output;
    for (const auto& point : input.points())
        if (contains(point.x, point.y, point.z) != invert) output.points().push_back(point);
    return output;
}

PointSet SpatialData::project(const PointSet& input) const {
    if (kind_ != Kind::Heightfield && kind_ != Kind::MeshSurface) return input;
    PointSet output = input;
    for (auto& point : output.points()) {
        if (point.x < bounds_.minX || point.x > bounds_.maxX || point.z < bounds_.minZ ||
            point.z > bounds_.maxZ)
            continue;
        if (kind_ == Kind::MeshSurface) {
            float y = 0.f, nx = 0.f, ny = 1.f, nz = 0.f;
            if (!closestMeshHeight(*mesh_, point.x, point.z, point.y, y, nx, ny, nz)) continue;
            point.y = y;
            point.normalX = nx;
            point.normalY = ny;
            point.normalZ = nz;
            continue;
        }
        const float hx = (point.x - originX_) / cellSize_;
        const float hz = (point.z - originZ_) / cellSize_;
        point.y        = heightmap_->sampleBilinear(hx, hz) * heightScale_;
        const float left  = heightmap_->sampleBilinear(hx - 0.5f, hz) * heightScale_;
        const float right = heightmap_->sampleBilinear(hx + 0.5f, hz) * heightScale_;
        const float down  = heightmap_->sampleBilinear(hx, hz - 0.5f) * heightScale_;
        const float up    = heightmap_->sampleBilinear(hx, hz + 0.5f) * heightScale_;
        float nx = -(right - left) / cellSize_;
        float ny = 1.f;
        float nz = -(up - down) / cellSize_;
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

}  // namespace eve::procgen
