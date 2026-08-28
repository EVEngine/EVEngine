#include "procgen/algorithms/PrototypeKitPrimitives.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace eve::procgen::prototype_detail {
namespace {

constexpr float kPi = 3.14159265358979323846f;

uint32_t addVertex(MeshBuild& mesh, Vec3 p, Vec3 n, float u, float v) {
    const auto index = static_cast<uint32_t>(mesh.getVertexCount());
    mesh.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, u, v);
    return index;
}

struct UV {
    float u;
    float v;
};

UV projectUv(Vec3 p, Vec3 normal) {
    const float ax = std::abs(normal.x);
    const float ay = std::abs(normal.y);
    const float az = std::abs(normal.z);
    if (ay >= ax && ay >= az) return {p.x, p.z};
    if (ax >= az) return {p.z, p.y};
    return {p.x, p.y};
}

void addQuad(MeshBuild& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal) {
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    const UV       uvA  = projectUv(a, normal);
    const UV       uvB  = projectUv(b, normal);
    const UV       uvC  = projectUv(c, normal);
    const UV       uvD  = projectUv(d, normal);
    addVertex(mesh, a, normal, uvA.u, uvA.v);
    addVertex(mesh, b, normal, uvB.u, uvB.v);
    addVertex(mesh, c, normal, uvC.u, uvC.v);
    addVertex(mesh, d, normal, uvD.u, uvD.v);
    mesh.addTriangle(base, base + 1, base + 2);
    mesh.addTriangle(base, base + 2, base + 3);
}

Vec3 normalOf(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3  u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3  v{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3        n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 1.0e-6f) {
        n.x /= length;
        n.y /= length;
        n.z /= length;
    }
    return n;
}

}  // namespace

void addBox(MeshBuild& mesh, Vec3 center, Vec3 size, const char* group) {
    mesh.setActiveGroup(group);
    const float x0 = center.x - size.x * 0.5f;
    const float x1 = center.x + size.x * 0.5f;
    const float y0 = center.y - size.y * 0.5f;
    const float y1 = center.y + size.y * 0.5f;
    const float z0 = center.z - size.z * 0.5f;
    const float z1 = center.z + size.z * 0.5f;
    addQuad(mesh, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
    addQuad(mesh, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {0, 0, -1});
    addQuad(mesh, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0});
    addQuad(mesh, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {1, 0, 0});
    addQuad(mesh, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, 1, 0});
    addQuad(mesh, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
}

void addWedge(MeshBuild& mesh, Vec3 center, Vec3 size, bool riseAlongX, const char* group) {
    mesh.setActiveGroup(group);
    const float x0 = center.x - size.x * 0.5f;
    const float x1 = center.x + size.x * 0.5f;
    const float y0 = center.y - size.y * 0.5f;
    const float y1 = center.y + size.y * 0.5f;
    const float z0 = center.z - size.z * 0.5f;
    const float z1 = center.z + size.z * 0.5f;
    Vec3        a{x0, y0, z0}, b{x1, y0, z0}, c{x1, y1, z0};
    Vec3        d{x0, y0, z1}, e{x1, y0, z1}, f{x1, y1, z1};
    if (!riseAlongX) {
        a = {x1, y0, z0};
        b = {x1, y0, z1};
        c = {x1, y1, z1};
        d = {x0, y0, z0};
        e = {x0, y0, z1};
        f = {x0, y1, z1};
    }
    const uint32_t first = static_cast<uint32_t>(mesh.getVertexCount());
    const Vec3     n0    = normalOf(a, c, b);
    const UV       uvA   = projectUv(a, n0);
    const UV       uvC   = projectUv(c, n0);
    const UV       uvB   = projectUv(b, n0);
    addVertex(mesh, a, n0, uvA.u, uvA.v);
    addVertex(mesh, c, n0, uvC.u, uvC.v);
    addVertex(mesh, b, n0, uvB.u, uvB.v);
    mesh.addTriangle(first, first + 1, first + 2);
    const uint32_t second = static_cast<uint32_t>(mesh.getVertexCount());
    const Vec3     n1     = normalOf(d, e, f);
    const UV       uvD    = projectUv(d, n1);
    const UV       uvE    = projectUv(e, n1);
    const UV       uvF    = projectUv(f, n1);
    addVertex(mesh, d, n1, uvD.u, uvD.v);
    addVertex(mesh, e, n1, uvE.u, uvE.v);
    addVertex(mesh, f, n1, uvF.u, uvF.v);
    mesh.addTriangle(second, second + 1, second + 2);
    addQuad(mesh, a, b, e, d, {0, -1, 0});
    const Vec3 slopeNormal = normalOf(a, d, f);
    addQuad(mesh, a, d, f, c, slopeNormal);
    const Vec3 highNormal = normalOf(b, c, f);
    addQuad(mesh, b, c, f, e, highNormal);
}

void addCylinder(MeshBuild& mesh, Vec3 center, float radiusBottom, float radiusTop, float height, int sides,
                 const char* group) {
    mesh.setActiveGroup(group);
    sides              = std::clamp(sides, 3, 64);
    const float y0     = center.y - height * 0.5f;
    const float y1     = center.y + height * 0.5f;
    const float radius = (radiusBottom + radiusTop) * 0.5f;
    for (int i = 0; i < sides; ++i) {
        const float    a0 = 2.0f * kPi * float(i) / float(sides);
        const float    a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const Vec3     p0{center.x + std::cos(a0) * radiusBottom, y0, center.z + std::sin(a0) * radiusBottom};
        const Vec3     p1{center.x + std::cos(a1) * radiusBottom, y0, center.z + std::sin(a1) * radiusBottom};
        const Vec3     p2{center.x + std::cos(a1) * radiusTop, y1, center.z + std::sin(a1) * radiusTop};
        const Vec3     p3{center.x + std::cos(a0) * radiusTop, y1, center.z + std::sin(a0) * radiusTop};
        const Vec3     normal = normalOf(p0, p2, p1);
        const uint32_t side   = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, p0, normal, a0 * radius, y0);
        addVertex(mesh, p1, normal, a1 * radius, y0);
        addVertex(mesh, p2, normal, a1 * radius, y1);
        addVertex(mesh, p3, normal, a0 * radius, y1);
        mesh.addTriangle(side, side + 2, side + 1);
        mesh.addTriangle(side, side + 3, side + 2);
        const uint32_t bottom = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, {center.x, y0, center.z}, {0, -1, 0}, center.x, center.z);
        addVertex(mesh, p1, {0, -1, 0}, p1.x, p1.z);
        addVertex(mesh, p0, {0, -1, 0}, p0.x, p0.z);
        mesh.addTriangle(bottom, bottom + 2, bottom + 1);
        const uint32_t top = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, {center.x, y1, center.z}, {0, 1, 0}, center.x, center.z);
        addVertex(mesh, p3, {0, 1, 0}, p3.x, p3.z);
        addVertex(mesh, p2, {0, 1, 0}, p2.x, p2.z);
        mesh.addTriangle(top, top + 2, top + 1);
    }
}

void addSphere(MeshBuild& mesh, Vec3 center, Vec3 radii, int rings, int sides, const char* group) {
    mesh.setActiveGroup(group);
    rings               = std::clamp(rings, 2, 32);
    sides               = std::clamp(sides, 3, 64);
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    for (int ring = 0; ring <= rings; ++ring) {
        const float v   = float(ring) / float(rings);
        const float phi = v * kPi;
        for (int side = 0; side <= sides; ++side) {
            const float u     = float(side) / float(sides);
            const float theta = u * 2.0f * kPi;
            const Vec3  n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            const float horizontalRadius = (radii.x + radii.z) * 0.5f;
            addVertex(mesh, {center.x + n.x * radii.x, center.y + n.y * radii.y, center.z + n.z * radii.z}, n,
                      theta * horizontalRadius, phi * radii.y);
        }
    }
    for (int ring = 0; ring < rings; ++ring) {
        for (int side = 0; side < sides; ++side) {
            const uint32_t a = base + uint32_t(ring * (sides + 1) + side);
            const uint32_t b = a + uint32_t(sides + 1);
            mesh.addTriangle(a, a + 1, b);
            mesh.addTriangle(a + 1, b + 1, b);
        }
    }
}

void addTorus(MeshBuild& mesh, Vec3 center, float majorRadius, float minorRadius, int majorSegments, int minorSegments,
              const char* group) {
    mesh.setActiveGroup(group);
    majorSegments       = std::clamp(majorSegments, 3, 64);
    minorSegments       = std::clamp(minorSegments, 3, 32);
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    for (int major = 0; major <= majorSegments; ++major) {
        const float u = float(major) / float(majorSegments);
        const float a = 2.0f * kPi * u;
        for (int minor = 0; minor <= minorSegments; ++minor) {
            const float v = float(minor) / float(minorSegments);
            const float b = 2.0f * kPi * v;
            const Vec3  n{std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)};
            const float radial = majorRadius + minorRadius * std::cos(b);
            addVertex(mesh,
                      {center.x + radial * std::cos(a), center.y + minorRadius * std::sin(b),
                       center.z + radial * std::sin(a)},
                      n, a * majorRadius, b * minorRadius);
        }
    }
    for (int major = 0; major < majorSegments; ++major) {
        for (int minor = 0; minor < minorSegments; ++minor) {
            const uint32_t a = base + uint32_t(major * (minorSegments + 1) + minor);
            const uint32_t b = a + uint32_t(minorSegments + 1);
            mesh.addTriangle(a, a + 1, b);
            mesh.addTriangle(a + 1, b + 1, b);
        }
    }
}

}  // namespace eve::procgen::prototype_detail
