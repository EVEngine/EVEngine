#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace eve::particles {

/** Color gradient stop at normalized lifetime t in [0, 1]. */
struct ColorStop {
    float t = 0.f;
    float r = 1.f;
    float g = 1.f;
    float b = 1.f;
    float a = 1.f;
};

/** Scalar curve point at normalized lifetime t in [0, 1]. */
struct CurvePoint {
    float t = 0.f;
    float v = 0.f;
};

/**
 * Multi-stop color gradient sampled with linear interpolation between stops.
 * Stops are kept sorted by t. Empty gradient means "use the legacy
 * colorStart/colorEnd two-point lerp".
 */
class ParticleGradient {
public:
    void clear() { stops.clear(); }

    void add(float t, float r, float g, float b, float a) {
        stops.push_back(ColorStop{t, r, g, b, a});
        std::sort(stops.begin(), stops.end(),
                  [](const ColorStop &x, const ColorStop &y) { return x.t < y.t; });
    }

    bool empty() const { return stops.empty(); }
    size_t size() const { return stops.size(); }
    const ColorStop &at(size_t i) const { return stops[i]; }

    void sample(float t, float &r, float &g, float &b, float &a) const {
        if (stops.empty()) {
            r = g = b = a = 1.f;
            return;
        }
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        if (t <= stops.front().t) {
            const ColorStop &s = stops.front();
            r = s.r;
            g = s.g;
            b = s.b;
            a = s.a;
            return;
        }
        if (t >= stops.back().t) {
            const ColorStop &s = stops.back();
            r = s.r;
            g = s.g;
            b = s.b;
            a = s.a;
            return;
        }
        for (size_t i = 1; i < stops.size(); ++i) {
            if (t > stops[i].t) continue;
            const ColorStop &p = stops[i - 1];
            const ColorStop &n = stops[i];
            const float span = n.t - p.t;
            const float f = span > 0.f ? (t - p.t) / span : 0.f;
            r = p.r + (n.r - p.r) * f;
            g = p.g + (n.g - p.g) * f;
            b = p.b + (n.b - p.b) * f;
            a = p.a + (n.a - p.a) * f;
            return;
        }
        r = g = b = a = 1.f;
    }

private:
    std::vector<ColorStop> stops;
};

/**
 * Multi-point scalar curve sampled with linear interpolation between points.
 * Empty curve means "use the legacy two-point linear fallback".
 */
class ParticleCurve {
public:
    void clear() { points.clear(); }

    void add(float t, float v) {
        points.push_back(CurvePoint{t, v});
        std::sort(points.begin(), points.end(),
                  [](const CurvePoint &x, const CurvePoint &y) { return x.t < y.t; });
    }

    bool empty() const { return points.empty(); }
    size_t size() const { return points.size(); }
    const CurvePoint &at(size_t i) const { return points[i]; }

    /** Sample at normalized t; fallback is returned when the curve is empty. */
    float sample(float t, float fallback) const {
        if (points.empty()) return fallback;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        if (t <= points.front().t) return points.front().v;
        if (t >= points.back().t) return points.back().v;
        for (size_t i = 1; i < points.size(); ++i) {
            if (t > points[i].t) continue;
            const CurvePoint &p = points[i - 1];
            const CurvePoint &n = points[i];
            const float span = n.t - p.t;
            const float f = span > 0.f ? (t - p.t) / span : 0.f;
            return p.v + (n.v - p.v) * f;
        }
        return fallback;
    }

private:
    std::vector<CurvePoint> points;
};

}  // namespace eve::particles
