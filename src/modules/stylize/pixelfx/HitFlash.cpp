// eve-hot-path
// Native adaptation of pixelfx impacts/hit-flash.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
enum class Beat { White, White2, Red, Rim, Cool, Done };
struct HitLine {
    double angle, distance, length, delay;
};
struct HitFlash final : Kernel {
    const std::vector<uint32_t> C;
    const PixelFxActor          actor;
    const double                cx, cy, hx, hy;
    std::vector<uint32_t>       reds, tints;
    std::vector<HitLine>        lines;
    Lights                      lights;
    double                      t = 0;
    explicit HitFlash(const PixelFxSettings& o)
        : C(o.palette),
          actor(o.actor),
          cx(actor.chest[0]),
          cy(actor.chest[1]),
          hx(cx + o.impact.side * 12),
          hy(cy - 5),
          reds(actor.pixels.size()),
          tints(actor.pixels.size()) {
        lights.add(hx, hy, 34, WHITE, .6, .1);
        lights.add(cx, cy, 30, C[4], .5, .35);
        for (size_t i = 0; i < actor.pixels.size(); ++i) {
            const auto c = actor.pixels[i];
            if (!c) continue;
            const double l = luma(c | 0xff000000u);
            reds[i]        = C[size_t(clamp(std::floor(2 + l * 5 + .5), 2, 6))];
            tints[i]       = mix(c | 0xff000000u, C[4], .55);
        }
        Rng rng(o.seed);
        lines.reserve(size_t(o.impact.lines));
        for (int i = 0; i < o.impact.lines; ++i) {
            const double a = -PI - .3 + ((i + rng.range(.25, .75)) / o.impact.lines) * (PI + .6), d = rng.range(15, 19),
                         len = rng.range(6, 9), delay = rng.range(0, .04);
            lines.push_back({a, d, len, delay});
        }
    }
    Beat beat() const {
        return t < .025   ? Beat::White
               : t < .058 ? Beat::White2
               : t < .125 ? Beat::Red
               : t < .158 ? Beat::Rim
               : t < .36  ? Beat::Cool
                          : Beat::Done;
    }
    template <class F>
    void body(Paint& p, F color) const {
        p.mask({actor.width, actor.height, actor.mask}, actor.x, actor.y,
               [&](double x, double y) { return color(size_t(y) * size_t(actor.width) + size_t(x)); });
    }
    void outline(Paint& p, uint32_t color, double alpha = 1) const {
        p.outline({actor.width, actor.height, actor.mask}, actor.x, actor.y, color, alpha);
    }
    void update(double dt) override {
        t += dt;
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto b = beat();
        if (b == Beat::White || b == Beat::White2) {
            body(p, [](size_t) { return WHITE; });
            outline(p, b == Beat::White ? C[6] : C[5]);
        } else if (b == Beat::Red) {
            body(p, [&](size_t i) { return reds[i]; });
            outline(p, C[5]);
        } else if (b == Beat::Rim)
            outline(p, WHITE);
        else if (b == Beat::Cool) {
            const double k = 1 - (t - .158) / (.36 - .158);
            body(p, [&](size_t i) { return mix(actor.pixels[i] | 0xff000000u, tints[i], k); });
            if (k > .5) outline(p, C[4], (k - .5) * 2);
        }
        if (t < .07) {
            const double big = t < .035 ? 1 : 1.3, k = t < .035 ? 0 : 1;
            for (int i = 0; i < 8; ++i) {
                const double a = (double(i) / 8) * TAU + .2, L = (i % 2 ? 4 : 9) * big;
                p.lineFn(
                    hx + std::cos(a) * L * .45 * k, hy + std::sin(a) * L * .45 * k, hx + std::cos(a) * L,
                    hy + std::sin(a) * L, [&](double u) { return u < .45 && !k ? WHITE
                                                                 : u < .75     ? C[6]
                                                                               : C[4]; }, 1.);
            }
            if (!k)
                for (const auto& d : std::array<Point, 5>{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}})
                    p.dot(hx + d.x, hy + d.y, WHITE);
            p.glow(hx, hy, 14, C[6], .5 * (1 - t / .07));
        }
        for (const auto& l : lines) {
            const double u = (t - l.delay) / .16;
            if (u < 0 || u >= 1) continue;
            const double d0 = l.distance - 6 * ease::outCubic(u), L = l.length * (1 - u), ca = std::cos(l.angle),
                         sa = std::sin(l.angle) * 1.15;
            if (L < 1) continue;
            const double               w = (2.8 - 1.6 * u) / 2, nx = -sa * w, ny = ca * w, ex = cx + ca * (d0 + L),
                                       ey = cy + sa * (d0 + L), px = cx + ca * d0, py = cy + sa * d0;
            const std::array<Point, 3> outer{{{px - ca, py - sa},
                                              {ex + nx * 1.5 + ca, ey + ny * 1.5 + sa},
                                              {ex - nx * 1.5 + ca, ey - ny * 1.5 + sa}}};
            const std::array<Point, 3> inner{{{px, py}, {ex + nx, ey + ny}, {ex - nx, ey - ny}}};
            p.poly(outer, C[3]);
            p.poly(inner, u < .5 ? WHITE : C[6]);
        }
        const double k = std::exp(-t / .08);
        if (k > .02) p.glow(cx, cy, 26, b == Beat::White || b == Beat::White2 ? WHITE : C[5], .35 * k);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& hitFlashDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"lines", 0, 128, false, 0, &PixelFxImpactSettings::lines},
                                                {"side", -8, 8, false, 0, &PixelFxImpactSettings::side}}};
    static const Descriptor            d{
        {PixelFxKind::HitFlash, "impacts/hit-flash", 80, 64, 40, 54, .5, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 17;
            s.actor.source = PixelFxActorSource::Dummy;
            s.impact.lines = 7;
            s.impact.side  = 1;
            s.palette      = {hex(0x14040a), hex(0x3a0a14), hex(0x6a101c), hex(0xa01824),
                              hex(0xd42a30), hex(0xff5a50), hex(0xff9a8a)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HitFlash>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
