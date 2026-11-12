// eve-hot-path
// Native adaptation of pixelfx lightning/electrified.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Jolt {
    int    f;
    double k;
};
constexpr std::array<Jolt, 3> JOLTS{{{3, 1}, {22, .7}, {41, .9}}};
struct Active {
    int    a = -1;
    double k = 0;
};
Active jolt(uint32_t f) {
    for (const auto& j : JOLTS)
        if (f >= uint32_t(j.f) && f < uint32_t(j.f + 3)) return {int(f) - j.f, j.k};
    return {};
}
struct Electrified final : AnalyticKernel {
    std::vector<Point> edges;
    explicit Electrified(const PixelFxActor& A) {
        const auto on = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < A.width && y < A.height &&
                   A.mask[size_t(y) * size_t(A.width) + size_t(x)] != 0;
        };
        for (int y = 0; y < A.height; ++y)
            for (int x = 0; x < A.width; ++x)
                if (on(x, y) && (!on(x - 1, y) || !on(x + 1, y) || !on(x, y - 1) || !on(x, y + 1)))
                    edges.push_back({A.x + x + .5, A.y + y + .5});
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        if (edges.empty()) return;
        const auto& C = o.palette;
        const auto& A = o.actor;
        const auto  f = jsUint(wrap(std::floor(t * 30 + 1e-6), 60));
        const auto  j = jolt(f);
        Rng         r(hash({o.seed, 4, f}));
        p.outline({A.width, A.height, A.mask}, A.x, A.y, j.a >= 0 ? C[4] : r.next() < .5 ? C[2] : C[1]);
        p.glow(0, -18, 30, C[3], j.a >= 0 ? .55 * j.k : .16 + .1 * r.next());
        p.glow(0, -2, 22, C[4], .18);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        if (edges.empty()) return;
        const auto&    C = o.palette;
        const auto&    S = o.lightning.sparks;
        const auto&    A = o.actor;
        const uint32_t f = jsUint(wrap(std::floor(t * 30 + 1e-6), 60));
        const auto     j = jolt(f);
        if (j.a >= 0) {
            const double                  lift = std::array<double, 3>{{1.1, .6, -.2}}[size_t(j.a)] * j.k;
            const std::array<uint32_t, 7> ramp{{C[1], C[2], C[3], C[4], C[5], C[6], C[7]}};
            for (int y = 0; y < A.height; ++y)
                for (int x = 0; x < A.width; ++x) {
                    const auto c = A.pixels[size_t(y) * size_t(A.width) + size_t(x)];
                    if (c) p.px(A.x + x, A.y + y, ramp[size_t(clamp(jsRound(.4 + luma(c) * 6 + lift), 0, 6))]);
                }
            p.glow(0, -19, 26, C[5], .5 * j.k * (1 - j.a / 3.));
        }
        Rng       r(hash({o.seed, 7, f >> 1}));
        const int n = 2 + (j.a >= 0 ? 2 : r.integer(0, 1));
        for (int i = 0; i < n; ++i) {
            const auto   a   = edges[size_t(std::floor(r.next() * double(edges.size())))];
            const double ang = r.range(0, TAU), d = r.range(6, 14);
            Point        b{a.x + std::cos(ang) * d, a.y + std::sin(ang) * d};
            for (int k = 0; k < 10; ++k) {
                const auto c = edges[size_t(std::floor(r.next() * double(edges.size())))];
                if (jsHypot2(c.x - b.x, c.y - b.y) < 5 && jsHypot2(c.x - a.x, c.y - a.y) > 4) b = c;
            }
            const double k = (f & 1u ? .75 : .95) * r.range(.6, 1);
            Rng          shape(hash({o.seed, 8, f, uint32_t(i)}));
            const auto   arc = lightning::zigzag(shape, a.x, a.y, b.x, b.y, .42, .35);
            lightning::crackle(p, arc, k, C, .4);
            for (const auto v : edges)
                if (jsHypot2(v.x - a.x, v.y - a.y) < 3.5 || jsHypot2(v.x - b.x, v.y - b.y) < 3)
                    p.px(v.x - .5, v.y - .5, C[5]);
        }
        if (r.next() < (j.a >= 0 ? .9 : .35)) {
            const int    s  = r.sign();
            const double x0 = s * r.range(3, 8), x1 = x0 + s * r.range(5, 12);
            Rng          shape(hash({o.seed, 9, f}));
            const auto   arc = lightning::zigzag(shape, x0, -1.5, x1, -.5, .45, .3);
            lightning::crackle(p, arc, .7, C, .35);
            p.glow(x0, -1, 10, C[5], .35);
        }
        for (const auto& event : JOLTS) {
            const double age = wrap(t - event.f / 30., 2);
            for (uint32_t i = 0; i < 9; ++i) {
                Rng          q(hash({o.seed, 21, uint32_t(event.f), i}));
                const double life = q.range(.18, .4);
                if (age >= life) continue;
                const auto   v   = edges[size_t(std::floor(q.next() * double(edges.size())))];
                const double ang = std::atan2(v.y + 19, v.x) + q.range(-.7, .7), sp = q.range(60, 150) * event.k;
                lightning::spark(p, lightning::flight(v.x, v.y, std::cos(ang) * sp, std::sin(ang) * sp - 30, age, 0),
                                 1 - age / life, S);
            }
        }
        cycle(t, 2, 8, o.seed + 3, .15, .3, [&](double age, double life, Rng& q, int) {
            const auto   v   = edges[size_t(std::floor(q.next() * double(edges.size())))];
            const double ang = std::atan2(v.y + 19, v.x) + q.range(-.6, .6), sp = q.range(50, 110);
            lightning::spark(p, lightning::flight(v.x, v.y, std::cos(ang) * sp, std::sin(ang) * sp - 20, age, 0),
                             1 - age / life, S);
        });
    }
};
}  // namespace
const Descriptor& electrifiedDescriptor() {
    static const std::array<Option, 2> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks},
                                                {"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{{PixelFxKind::Electrified, "lightning/electrified", 108, 92, 54, 76, 2, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed = 5;
                                  s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
                                  s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
                                  s.actor.source = PixelFxActorSource::Dummy;
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Electrified>(s.actor);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
