#include "procgen/texture/ColorRamp.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {

void ColorRamp::add(float t, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    stops.push_back({t, {r, g, b, a}});
    std::sort(stops.begin(), stops.end(),
              [](const Stop &a, const Stop &b) { return a.t < b.t; });
}

Rgba8 ColorRamp::sample(float t) const {
    if (stops.empty()) return {};
    t = std::clamp(t, 0.f, 1.f);
    if (t <= stops.front().t) return stops.front().c;
    if (t >= stops.back().t) return stops.back().c;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].t) {
            const Stop &a = stops[i - 1];
            const Stop &b = stops[i];
            const float u = (t - a.t) / std::max(1e-6f, b.t - a.t);
            auto lerp8 = [&](uint8_t x, uint8_t y) {
                return uint8_t(std::lround(float(x) + (float(y) - float(x)) * u));
            };
            return {lerp8(a.c.r, b.c.r), lerp8(a.c.g, b.c.g), lerp8(a.c.b, b.c.b),
                    lerp8(a.c.a, b.c.a)};
        }
    }
    return stops.back().c;
}

Rgba8 ColorRamp::sampleBanded(float t, int bands) const {
    bands = std::max(2, bands);
    t     = std::clamp(t, 0.f, 1.f);
    const float step = 1.f / float(bands - 1);
    const int   idx  = int(std::lround(t / step));
    return sample(float(idx) * step);
}

}  // namespace eve::procgen
