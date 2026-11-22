#include "procgen/mesh/VesselGenerator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace eve::procgen {
namespace {
double cross(VesselSectionPoint a, VesselSectionPoint b, VesselSectionPoint c) {
    return (double(b.radius) - a.radius) * (double(c.height) - a.height) -
           (double(b.height) - a.height) * (double(c.radius) - a.radius);
}
bool intersects(VesselSectionPoint a, VesselSectionPoint b, VesselSectionPoint c, VesselSectionPoint d) {
    if (std::max(a.radius, b.radius) < std::min(c.radius, d.radius) ||
        std::max(c.radius, d.radius) < std::min(a.radius, b.radius) ||
        std::max(a.height, b.height) < std::min(c.height, d.height) ||
        std::max(c.height, d.height) < std::min(a.height, b.height))
        return false;
    return cross(a, b, c) * cross(a, b, d) <= 0. && cross(c, d, a) * cross(c, d, b) <= 0.;
}
}  // namespace

eve::Result<MeshBuild> revolveVesselSection(const std::vector<VesselSectionPoint>& section, int radialSegments) {
    auto invalid = [] {
        return eve::Result<MeshBuild>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "VesselSectionInvalid: Expected a finite simple counterclockwise solid section and "
                                   "3..512 radial segments"));
    };
    const auto count = section.size();
    if (count < 3 || count > 1024 || radialSegments < 3 || radialSegments > 512) return invalid();
    if (section.front().radius != 0.f || section.back().radius != 0.f) return invalid();
    double area = 0.;
    for (size_t i = 0; i < count; ++i) {
        const auto a = section[i], b = section[(i + 1) % count];
        if (!std::isfinite(a.radius) || !std::isfinite(a.height) || a.radius < 0.f || std::abs(a.height) > 10000.f ||
            a.radius > 10000.f || (i > 0 && i + 1 < count && a.radius < 0.00001f))
            return invalid();
        if (std::hypot(double(b.radius) - a.radius, double(b.height) - a.height) < 0.00001) return invalid();
        area += double(a.radius) * b.height - double(b.radius) * a.height;
        for (size_t j = i + 2; j < count; ++j) {
            if (i == 0 && j + 1 == count) continue;
            if (intersects(a, b, section[j], section[(j + 1) % count])) return invalid();
        }
    }
    if (area <= 0.00000001) return invalid();
    MeshBuild mesh;
    mesh.reserve(int((count - 1) * 2 * (radialSegments + 1)), int((count - 1) * radialSegments * 6));
    mesh.setActiveGroup("ceramic");
    double total = 0.;
    for (size_t i = 1; i < count; ++i)
        total += std::hypot(double(section[i].radius) - section[i - 1].radius,
                            double(section[i].height) - section[i - 1].height);
    double distance = 0.;
    for (size_t i = 1; i < count; ++i) {
        const auto   a = section[i - 1], b = section[i];
        const double dr = double(b.radius) - a.radius, dy = double(b.height) - a.height;
        const double length = std::hypot(dr, dy);
        const auto   base   = static_cast<uint32_t>(mesh.getVertexCount());
        // Blend nearby section tangents; retain creases at the base and sharp shoulders.
        double nr[2] = {dy / length, dy / length};
        double ny[2] = {-dr / length, -dr / length};
        for (int end = 0; end < 2; ++end) {
            const size_t point = i - 1 + size_t(end);
            if (point == 0 || point + 1 == count) continue;
            const auto   c  = end == 0 ? section[point - 1] : section[point];
            const auto   d  = end == 0 ? section[point] : section[point + 1];
            const double cr = double(d.radius) - c.radius, cy = double(d.height) - c.height;
            const double cl = std::hypot(cr, cy);
            if ((dr * cr + dy * cy) / (length * cl) > 0.8) {
                const double x = nr[end] + cy / cl, y = ny[end] - cr / cl;
                const double norm = std::hypot(x, y);
                nr[end]           = x / norm;
                ny[end]           = y / norm;
            }
        }
        for (int j = 0; j <= radialSegments; ++j) {
            const double angle = j == radialSegments ? 0. : 6.283185307179586 * j / radialSegments;
            const float  c = float(std::cos(angle)), s = float(std::sin(angle));
            for (int end = 0; end < 2; ++end) {
                const auto p = end == 0 ? a : b;
                mesh.addVertex(p.radius * c, p.height, p.radius * s, float(nr[end]) * c, float(ny[end]),
                               float(nr[end]) * s, float(j) / radialSegments, float((distance + end * length) / total));
            }
        }
        for (int j = 0; j < radialSegments; ++j) {
            const auto k = base + uint32_t(j * 2);
            if (a.radius > 0.f) mesh.addTriangle(k, k + 1, k + 2);
            if (b.radius > 0.f) mesh.addTriangle(k + 1, k + 3, k + 2);
        }
        distance += length;
    }
    mesh.setMeta("generator", "mesh.vessel");
    mesh.setMeta("axis", "Y");
    return eve::Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace eve::procgen
