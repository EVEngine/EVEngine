// eve-hot-path
// Native adaptation of pixelfx holy/divine-shield.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include <unordered_map>
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 2.4, CY = -22, TILT = .34, N = 4;
struct SpherePoint {
    double x, y, z;
};
SpherePoint onSphere(double r, double lat, double lon) {
    const double X = r * std::cos(lat) * std::sin(lon), Y = r * std::sin(lat), Z = r * std::cos(lat) * std::cos(lon);
    return {X, CY - (Y * std::cos(TILT) - Z * std::sin(TILT)), Y * std::sin(TILT) + Z * std::cos(TILT)};
}
double              sheen(double t) { return -1.7 + 3.4 * clamp(std::fmod(t / D, 1) / .55); }
std::vector<double> samples(double from, double to, double step, bool inclusive) {
    std::vector<double> out;
    for (double v = from; inclusive ? v <= to : v < to; v += step) out.push_back(v);
    return out;
}
struct DivineShield final : AnalyticKernel {
    const holy::ActorShape    shape;
    const std::vector<double> latitudes, equator;
    explicit DivineShield(const PixelFxSettings& o)
        : shape(holy::prepareShape(o.actor)),
          latitudes(samples(-PI / 2, PI / 2, .7 / o.holy.radius, true)),
          equator(samples(0, TAU, .7 / o.holy.radius, false)) {}
    void lattice(Paint& p, double t, const PixelFxSettings& o, bool back) const {
        const auto&  C = o.palette;
        const double r = o.holy.radius, phi = (t / D) * (PI / N), sw = sheen(t);
        struct Dot {
            double x, y, f;
            bool   hot;
        };
        std::vector<Dot>                    dots;
        std::unordered_map<int64_t, size_t> seen;
        const auto                          dot = [&](SpherePoint point) {
            const auto [x, y, z] = point;
            if ((z < 0) != back) return;
            const auto   key = int64_t(std::floor(x)) * 1000 + int64_t(std::floor(y));
            const double f = std::abs(z) / r, q = (x - (y - CY)) / (r * 1.414);
            const bool   hot   = std::abs(q - sw) < .12;
            auto         found = seen.find(key);
            if (found == seen.end()) {
                seen.emplace(key, dots.size());
                dots.push_back({x, y, f, hot});
            } else {
                auto& previous = dots[found->second];
                if (f < previous.f) previous = {x, y, f, hot || previous.hot};
            }
        };
        for (int k = 0; k < 2 * N; ++k) {
            const double lon = phi + (k * PI) / N;
            for (double lat : latitudes) dot(onSphere(r, lat, lon));
        }
        for (double lon : equator) dot(onSphere(r, 0, lon));
        for (const auto& v : dots) {
            if (back)
                p.blend(v.x, v.y, C[4], .32);
            else if (v.hot)
                p.px(v.x, v.y, WHITE);
            else if (v.f < .4)
                p.px(v.x, v.y, C[6]);
            else
                p.blend(v.x, v.y, v.f < .7 ? C[6] : C[5], v.f < .7 ? .8 : .55);
        }
    }
    void film(Paint& p, double t, const PixelFxSettings& o, bool back) const {
        const auto&  C = o.palette;
        const double r = o.holy.radius, sw = sheen(t);
        p.shade(-r - 1, CY - r - 1, r + 1, CY + r + 1, [&](double x, double y, int sx, int sy) {
            const double d = jsHypot2(x, y - CY) / r;
            if (d >= 1) return;
            const double q = (x - (y - CY)) / (r * 1.414), hot = std::max(0., 1 - std::abs(q - sw) / .22),
                         a = (back ? .07 : .03) + .26 * std::pow(d, 6) + .2 * hot * (back ? .3 : 1);
            p.blendPx(sx, sy, hot > .5 && !back ? C[6] : C[5], a);
        });
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const double r = o.holy.radius, rf = std::sqrt(r * r - CY * CY);
        film(p, t, o, true);
        lattice(p, t, o, true);
        holy::oval(p, 0, 0, rf, rf * .26, C[5], .8, [](double g) { return std::sin(g) < 0; });
        p.glow(0, CY, r * 1.1, C[4], .14);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C = o.palette;
        const double r = o.holy.radius, rf = std::sqrt(r * r - CY * CY);
        if (o.actor.source != PixelFxActorSource::None)
            for (auto point : shape.out) p.light(point.x, point.y, C[5], .3);
        film(p, t, o, false);
        lattice(p, t, o, false);
        const double sw = sheen(t);
        const double n  = std::ceil(r * TAU * 1.4);
        for (double j = 0; j < n; ++j) {
            const double g = (j / n) * TAU, x = std::cos(g) * r, y = CY + std::sin(g) * r,
                         q = (x - (y - CY)) / (r * 1.414), lit = std::cos(g + 2.36);
            const bool   hot = std::abs(q - sw) < .15;
            p.dot(x, y, hot || lit > .75 ? WHITE : lit > -.2 ? C[6] : C[5], 1);
            p.dot(std::cos(g) * (r - 1), CY + std::sin(g) * (r - 1), C[4], .35 + .3 * lit);
        }
        p.glowRing(0, CY, r, 6, C[4], .32);
        const double phi = (t / D) * (PI / N);
        for (int k = 0; k < 2 * N; ++k) {
            const auto [x, y, z] = onSphere(r, 0, phi + (k * PI) / N);
            if (z < r * .15) continue;
            p.px(x, y, WHITE);
            if (z > r * .45)
                for (auto v : std::array<Point, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.blend(x + v.x, y + v.y, C[6], .7);
            p.glow(x, y, 4, C[5], .3);
        }
        const auto pole = onSphere(r, PI / 2, 0);
        p.sparkle(pole.x, pole.y, 2 + jsRound(1.5 + 1.5 * std::sin((t / D) * TAU * 2)), WHITE, C[6], .45);
        for (double off : {0., PI}) {
            const double g = (t / D) * TAU + off + .8;
            p.sparkle(std::cos(g) * r, CY + std::sin(g) * r, 2, WHITE, C[6], .4);
        }
        for (double g = 3.55; g < 4.2; g += .04)
            p.px(std::cos(g) * r * .74, CY + std::sin(g) * r * .74, g > 3.7 && g < 4.05 ? WHITE : C[6]);
        p.px(std::cos(3.4) * r * .62, CY + std::sin(3.4) * r * .62, WHITE);
        for (double g = .45; g < .8; g += .05) p.dot(std::cos(g) * r * .8, CY + std::sin(g) * r * .8, C[5], .6);
        holy::oval(
            p, 0, 0, rf, rf * .26, [&](double g) { return std::sin(g) > .6 ? WHITE : C[6]; }, 1.,
            [](double g) { return std::sin(g) >= 0; });
        if (std::isfinite(rf))
            for (int i = -2; i <= 2; ++i) p.glow(i * rf * .5, 0, 12, C[4], .18);
        cycle(t, D, 9, o.seed, .35, .6, [&](double age, double life, Rng& R, int) {
            const double lat = R.range(-1.2, 1.3), lon = R.range(0, TAU) + phi;
            const auto [x, y, z] = onSphere(r, lat, lon);
            const double k       = std::sin((age / life) * PI);
            if (z < 0) return;
            const double size = 1 + 2.5 * k * R.range(.6, 1);
            p.sparkle(x, y, size, WHITE, C[6], .45 * k);
        });
        cycle(t, D, 12, o.seed + 1u, 1.2, 2.2, [&](double age, double life, Rng& R, int) {
            const double x0 = R.range(-.7, .7) * r, u = age / life, x = x0 + std::sin(u * 6 + R.range(0, 6)) * 2,
                         y = CY + r * .75 - u * r * 1.4;
            if (jsHypot2(x, y - CY) > r - 2) return;
            p.dot(x, y, u < .5 ? C[6] : C[5], std::sin(u * PI));
            p.light(x, y, C[5], .4 * std::sin(u * PI));
        });
    }
};
}  // namespace
const Descriptor& divineShieldDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"radius", 1, 256, false, 0, &PixelFxHolySettings::radius}}};
    static const Descriptor            d{{PixelFxKind::DivineShield, "holy/divine-shield", 88, 84, 44, 68, D, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 8;
                                  s.actor.source = PixelFxActorSource::Dummy;
                                  s.holy.radius  = 29;
                                  s.palette      = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                                                    hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<DivineShield>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
