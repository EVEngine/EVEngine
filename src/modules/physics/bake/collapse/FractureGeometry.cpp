#include "physics/bake/collapse/FractureGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
namespace eve::physics {
Result<void> FractureGeometry::validate() const {
    auto fail = [] {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "invalid closed convex fracture mesh or budget", "geometry"));
    };
    if (fragments.empty() || fragments.size() > 4096) return fail();
    std::size_t total = 0;
    for (const auto& f : fragments) {
        const auto count = f.positions.size() / 3;
        if (f.positions.size() % 3 || count < 4 || count > 254 || f.indices.empty() || f.indices.size() % 3 ||
            f.indices.size() > 8192 || f.materials.size() != f.indices.size() / 3)
            return fail();
        total += f.positions.size() + f.indices.size() + f.materials.size();
        if (total > 4000000) return fail();
        double scale = 0;
        for (float v : f.positions) {
            if (!std::isfinite(v)) return fail();
            scale = std::max(scale, std::abs(static_cast<double>(v)));
        }
        const double epsilon = std::max(1e-9, scale * 1e-6);
        auto         point   = [&](std::uint32_t i) {
            return std::array<double, 3>{f.positions[3 * i], f.positions[3 * i + 1], f.positions[3 * i + 2]};
        };
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edges;
        double                                                                 volume = 0;
        for (std::size_t t = 0; t < f.indices.size(); t += 3) {
            const auto ia = f.indices[t], ib = f.indices[t + 1], ic = f.indices[t + 2];
            if (ia >= count || ib >= count || ic >= count || ia == ib || ib == ic || ia == ic || f.materials[t / 3] > 1)
                return fail();
            auto                        a = point(ia), b = point(ib), c = point(ic);
            const std::array<double, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const std::array<double, 3> n{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                          ab[0] * ac[1] - ab[1] * ac[0]};
            const double                length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (length < epsilon * epsilon)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "degenerate fracture triangle", "geometry"));
            for (std::uint32_t i = 0; i < count; ++i) {
                auto q = point(i);
                if (n[0] * (q[0] - a[0]) + n[1] * (q[1] - a[1]) + n[2] * (q[2] - a[2]) > epsilon * length * 4)
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "fracture mesh is not convex", "geometry"));
            }
            volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) +
                       a[2] * (b[0] * c[1] - b[1] * c[0])) /
                      6;
            const std::uint32_t ids[] = {ia, ib, ic, ia};
            for (int k = 0; k < 3; ++k) {
                const auto x = ids[k], y = ids[k + 1];
                auto&      edge = edges[std::minmax(x, y)];
                ++edge.first;
                edge.second += x < y ? 1 : -1;
            }
        }
        if (!std::isfinite(volume) || volume <= epsilon * epsilon * epsilon) return fail();
        for (const auto& [key, value] : edges) {
            (void)key;
            if (value.first != 2 || value.second != 0)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                      "fracture mesh has open or inconsistently oriented edges", "geometry"));
        }
    }
    return Result<void>::success();
}
}  // namespace eve::physics
