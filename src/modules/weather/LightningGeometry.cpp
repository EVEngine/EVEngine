#include <algorithm>
#include <array>
#include <cmath>
#include "weather/Lightning.h"
namespace eve::weather {
Result<void> validateLightning(const LightningSettings& s) {
    if (!std::isfinite(s.width) || s.width <= 0 || s.width > 100 || !std::isfinite(s.displacement) ||
        s.displacement < 0 || s.displacement > 100 || !std::isfinite(s.duration) || s.duration <= 0 ||
        s.duration > 60 || s.subdivisions < 1 || s.subdivisions > 8 || s.branches < 0 || s.branches > 16 ||
        s.variants < 1 || s.variants > 16)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid lightning settings", "weather.lightning"));
    return Result<void>::success();
}
float lightningEnvelope(float age) {
    if (!std::isfinite(age) || age < 0 || age >= 1) return 0;
    const float t = age * .5f;
    if (t < .055f) return 1.f - t * 4.f;
    if (t < .095f) return .06f;
    if (t < .19f) return .72f * (1.f - (t - .095f) / .095f);
    return std::max(0.f, .22f * (1.f - (t - .19f) / .31f));
}
Result<LightningGeometry> buildLightningGeometry(const LightningSettings& s, unsigned variant) {
    auto valid = validateLightning(s);
    if (!valid) return Result<LightningGeometry>::failure(valid.status());
    using Pt             = std::array<float, 3>;
    std::uint32_t rng    = static_cast<std::uint32_t>(s.seed) + variant * 0x9e3779b9u;
    auto          random = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return float(rng >> 8u) / 16777216.f;
    };
    auto refine = [&](std::vector<Pt>& path, float displacement, int levels) {
        for (int level = 0; level < levels; ++level) {
            std::vector<Pt> next;
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                const auto a = path[i], b = path[i + 1];
                next.push_back(a);
                next.push_back({(a[0] + b[0]) * .5f + (random() * 2 - 1) * displacement, (a[1] + b[1]) * .5f,
                                (a[2] + b[2]) * .5f + (random() * 2 - 1) * displacement});
            }
            next.push_back(path.back());
            path.swap(next);
            displacement *= .48f;
        }
    };
    std::vector<Pt> main{{0, 0, 0}, {0, 1, 0}};
    refine(main, s.displacement, s.subdivisions);
    std::vector<std::vector<Pt>> paths{main};
    for (int b = 0; b < s.branches; ++b) {
        auto a = main[1 + (static_cast<size_t>(b + 1) * (main.size() - 2)) / (static_cast<size_t>(s.branches) + 1)];
        std::vector<Pt> branch{
            a,
            {a[0] + (random() * 2 - 1) * s.displacement * 1.8f, std::min(1.f, a[1] + .12f + random() * .15f),
             a[2] + (random() * 2 - 1) * s.displacement * 1.8f}};
        refine(branch, s.displacement * .34f, 3);
        paths.push_back(std::move(branch));
    }
    LightningGeometry g;
    for (size_t p = 0; p < paths.size(); ++p) {
        const auto& path = paths[p];
        for (size_t i = 0; i + 1 < path.size(); ++i)
            for (int plane = 0; plane < 2; ++plane) {
                const float t    = float(i) / float(path.size() - 1);
                const float w    = s.width * (p == 0 ? 1.f : .55f) * (1.f - .9f * t);
                const auto  base = static_cast<std::uint32_t>(g.positions.size() / 3);
                for (int c = 0; c < 4; ++c) {
                    auto point = path[i + (c >= 2 ? 1 : 0)];
                    point[plane == 0 ? 0 : 2] += (c == 0 || c == 3 ? w : -w);
                    g.positions.insert(g.positions.end(), point.begin(), point.end());
                    g.normals.insert(g.normals.end(), {0, 1, 0});
                    g.uv.insert(g.uv.end(), {c == 0 || c == 3 ? -1.f : 1.f, t});
                }
                g.indices.insert(g.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
    }
    return Result<LightningGeometry>::success(std::move(g));
}
}  // namespace eve::weather
