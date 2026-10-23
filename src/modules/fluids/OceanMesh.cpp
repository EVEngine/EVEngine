#include "fluids/OceanMesh.h"

#include <cmath>

namespace eve::fluids {
Result<OceanMeshData> OceanMeshBuilder::build() const {
    const auto& s = settings;
    if (s.segments < 16 || s.segments > 1024 || s.segments % 8 != 0 || s.rings < 2 || s.rings > 512 ||
        !std::isfinite(s.firstRadius) || !std::isfinite(s.outerRadius) || s.firstRadius < 0.01 ||
        s.outerRadius < s.firstRadius * s.rings || s.outerRadius > 1e6)
        return Result<OceanMeshData>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid ocean mesh extent or tessellation", "fluids.ocean.mesh"));

    // Geometric widths rather than geometric radii preserve a dense central patch.
    auto extent = [&](double growth) {
        double width = s.firstRadius, total = 0.0;
        for (unsigned ring = 0; ring < s.rings; ++ring) {
            total += width;
            width *= growth;
        }
        return total;
    };
    double lo = 1.0, hi = 1.0 + s.outerRadius / s.firstRadius;
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        const double mid = (lo + hi) * 0.5;
        if (extent(mid) < s.outerRadius)
            lo = mid;
        else
            hi = mid;
    }
    const double  growth = (lo + hi) * 0.5;
    OceanMeshData result;
    result.parameterOffsets.reserve(1 + s.segments * s.rings);
    result.indices.reserve(3 * s.segments * (2 * s.rings - 1));
    result.radii.reserve(s.rings);
    result.parameterOffsets.emplace_back(0.0);
    double radius = 0.0, width = s.firstRadius;
    for (unsigned ring = 0; ring < s.rings; ++ring) {
        radius += width;
        width *= growth;
        if (ring + 1 == s.rings) radius = s.outerRadius;
        result.radii.push_back(radius);
        // Equal spacing along each square side includes all four corners exactly.
        for (unsigned segment = 0; segment < s.segments; ++segment) {
            const unsigned side = segment / (s.segments / 4);
            const double   t    = 2.0 * (segment % (s.segments / 4)) / (s.segments / 4) - 1.0;
            glm::dvec2     direction;
            switch (side) {
                case 0: direction = {1.0, t}; break;
                case 1: direction = {-t, 1.0}; break;
                case 2: direction = {-1.0, -t}; break;
                default: direction = {t, -1.0}; break;
            }
            result.parameterOffsets.push_back(direction * radius);
            const auto outer = 1 + ring * s.segments + segment;
            const auto next  = 1 + ring * s.segments + (segment + 1) % s.segments;
            if (ring == 0)
                result.indices.insert(result.indices.end(), {0, next, outer});
            else {
                const auto inner     = outer - s.segments;
                const auto innerNext = next - s.segments;
                result.indices.insert(result.indices.end(), {inner, next, outer, inner, innerNext, next});
            }
        }
    }
    return Result<OceanMeshData>::success(std::move(result));
}
}  // namespace eve::fluids
