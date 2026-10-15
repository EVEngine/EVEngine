#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3  sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3  add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3  mul(Vec3 a, float scale) { return {a.x * scale, a.y * scale, a.z * scale}; }

EdgeKey makeEdgeKey(Vec3 a, Vec3 b) {
    auto quantize = [](Vec3 p) {
        return QuantizedPoint{int(std::lround(p.x * 1000000.f)), int(std::lround(p.y * 1000000.f)),
                              int(std::lround(p.z * 1000000.f)), 0};
    };
    QuantizedPoint qa = quantize(a), qb = quantize(b);
    const bool     swap = qa.x > qb.x || (qa.x == qb.x && qa.y > qb.y) || (qa.x == qb.x && qa.y == qb.y && qa.z > qb.z);
    if (swap) std::swap(qa, qb);
    return {qa.x, qa.y, qa.z, qb.x, qb.y, qb.z};
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float smoothMaximum(float a, float b, float blendRadius) {
    if (blendRadius <= 0.f) return std::max(a, b);
    const float h = std::clamp(0.5f + 0.5f * (a - b) / blendRadius, 0.f, 1.f);
    return b + (a - b) * h + blendRadius * h * (1.f - h);
}

float distanceToSegment(Vec3 point, Vec3 a, Vec3 b) {
    const Vec3  segment = sub(b, a);
    const float length2 = dot(segment, segment);
    const float t       = length2 > 1e-8f ? std::clamp(dot(sub(point, a), segment) / length2, 0.f, 1.f) : 0.f;
    const Vec3  delta   = sub(point, add(a, mul(segment, t)));
    return std::sqrt(dot(delta, delta));
}

float ellipsoidDistance(Vec3 point, const Chamber& shape) {
    const Vec3  q           = sub(point, shape.center);
    const float normalizedX = q.x / shape.radii.x;
    const float normalizedY = q.y / shape.radii.y;
    const float normalizedZ = q.z / shape.radii.z;
    const float base =
        (std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY + normalizedZ * normalizedZ) - 1.f) *
        std::min({shape.radii.x, shape.radii.y, shape.radii.z});
    if (shape.irregularity <= 0.f) return base;
    const float azimuth = std::atan2(normalizedZ, normalizedX);
    const float lobes   = std::sin(azimuth * 3.f + shape.phase) * 0.58f +
                        std::sin(azimuth * 2.f - normalizedY * 2.4f + shape.phase * 0.7f) * 0.42f;
    return base - lobes * shape.irregularity * std::min({shape.radii.x, shape.radii.y, shape.radii.z}) * 0.13f;
}

float taperedSegmentDistance(Vec3 point, const Dripstone& shape, float horizontalX, float horizontalZ) {
    const Vec3  axis    = sub(shape.end, shape.start);
    const float length2 = dot(axis, axis);
    const float t       = length2 > 1e-8f ? std::clamp(dot(sub(point, shape.start), axis) / length2, 0.f, 1.f) : 0.f;
    Vec3        delta   = sub(point, add(shape.start, mul(axis, t)));
    delta.x *= horizontalX;
    delta.z *= horizontalZ;
    float       radius   = shape.startRadius + (shape.endRadius - shape.startRadius) * t;
    const float envelope = std::sin(t * 3.1415926535f);
    radius *= 1.f + shape.profileAmplitude * envelope *
                        std::sin(t * shape.profileFrequency * 6.283185307f + shape.profilePhase);
    return std::sqrt(dot(delta, delta)) - radius;
}

float flowstoneDistance(Vec3 point, const Flowstone& shape) {
    const Vec3  q      = sub(point, shape.center);
    const float u      = dot(q, shape.tangent);
    const float v      = q.y;
    const float ripple = std::sin((v / shape.halfHeight) * shape.rippleFrequency + shape.phase) * shape.rippleAmplitude;
    const float n      = dot(q, shape.normal) - ripple;
    const float ellipsoid =
        std::sqrt((u * u) / (shape.halfWidth * shape.halfWidth) + (v * v) / (shape.halfHeight * shape.halfHeight) +
                  (n * n) / (shape.thickness * shape.thickness)) -
        1.f;
    return ellipsoid * std::min({shape.halfWidth, shape.halfHeight, shape.thickness});
}

float curtainDistance(Vec3 point, const Curtain& shape) {
    const Vec3  q        = sub(point, shape.anchor);
    const float u        = dot(q, shape.tangent);
    const float downward = -q.y;
    const float wave     = std::sin((u / shape.halfWidth) * shape.waveFrequency + shape.phase) * shape.waveAmplitude;
    const float n        = dot(q, shape.normal) - wave;
    const float side     = std::fabs(u) - shape.halfWidth;
    const float vertical = std::max(-downward, downward - shape.length);
    const float sheet    = std::fabs(n) - shape.thickness;
    return std::max({side, vertical, sheet});
}

uint32_t hash(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

float latticeNoise(float x, float y, float z, uint32_t seed) {
    const int   ix     = int(std::floor(x));
    const int   iy     = int(std::floor(y));
    const int   iz     = int(std::floor(z));
    const float fx     = x - float(ix);
    const float fy     = y - float(iy);
    const float fz     = z - float(iz);
    auto        smooth = [](float t) { return t * t * (3.f - 2.f * t); };
    auto        sample = [seed](int sx, int sy, int sz) {
        uint32_t h = hash(uint32_t(sx) * 73856093u ^ uint32_t(sy) * 19349663u ^ uint32_t(sz) * 83492791u ^ seed);
        return float(h & 0xffffu) / 32767.5f - 1.f;
    };
    const float ux = smooth(fx), uy = smooth(fy), uz = smooth(fz);
    auto        lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00  = lerp(sample(ix, iy, iz), sample(ix + 1, iy, iz), ux);
    const float x10  = lerp(sample(ix, iy + 1, iz), sample(ix + 1, iy + 1, iz), ux);
    const float x01  = lerp(sample(ix, iy, iz + 1), sample(ix + 1, iy, iz + 1), ux);
    const float x11  = lerp(sample(ix, iy + 1, iz + 1), sample(ix + 1, iy + 1, iz + 1), ux);
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz);
}

Vec3 projectToDensitySurface(Vec3 meshPoint, const std::vector<float>& density, int nx, int ny, int nz) {
    const CaveFieldPoint point =
        projectToCaveDensitySurface({meshPoint.x, meshPoint.y, meshPoint.z}, density, nx, ny, nz);
    return {point.x, point.y, point.z};
}

void addFacetedTriangle(MeshBuild& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 referenceNormal) {
    const Vec3 ab = sub(b, a), ac = sub(c, a);
    Vec3       normal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    if (dot(normal, referenceNormal) < 0.f) {
        std::swap(b, c);
        normal = mul(normal, -1.f);
    }
    const float length = std::sqrt(dot(normal, normal));
    if (length > 1e-8f) normal = mul(normal, 1.f / length);
    const uint32_t base = uint32_t(mesh.getVertexCount());
    mesh.addVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, a.x + 0.5f, a.y + 0.5f);
    mesh.addVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, b.x + 0.5f, b.y + 0.5f);
    mesh.addVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, c.x + 0.5f, c.y + 0.5f);
    mesh.addTriangle(base, base + 1, base + 2);
}

bool validStyle(const std::string& style) {
    return style == "cavern" || style == "tunnels" || style == "vertical" || style == "labyrinth" || style == "mixed";
}

bool validGenesis(const std::string& genesis) {
    return genesis == "epigene" || genesis == "hypogene" || genesis == "mixed";
}


}  // namespace eve::procgen::cave_detail
