// eve-hot-path
// Adapted from pixelfx fire/fireball.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<std::array<double, 3>, 4> BODY{{{30, 4.2, 0}, {23, 4.4, -1.6}, {22, 4.4, 1.6}, {15, 4.6, 0}}};
constexpr std::array<std::array<double, 3>, 6> CORONA{
    {{-1.3, 6, 1.1}, {-1.8, 8, 1.2}, {-2.3, 7, 1.1}, {1.3, 6, 1.1}, {1.8, 8, 1.2}, {2.3, 7, 1.1}}};
void head(Paint& p, std::span<const uint32_t> H, double t, double radius, uint32_t seed, double hx, double hy) {
    const double ph = t / 1.2;
    p.shader(hx - radius * 2.4, hy - radius * 1.7, hx + radius * 1.5, hy + radius * 1.7,
             [&](double x, double y, ShadeOutput& out) {
                 x -= hx;
                 y -= hy;
                 const double d = jsHypot2(x, y), th = std::atan2(y, x), back = std::max(0.0, -std::cos(th)),
                              a = th / TAU;
                 const double bump =
                     0.55 * fire::pnoise(a * 9 + ph * 9, 9, seed) + 0.35 * fire::pnoise(a * 5 - ph * 10, 5, seed + 1);
                 const double rim = radius * (1 + 0.55 * back * back) + bump;
                 if (d > rim) return;
                 const double swirl = fire::pnoise(a * 5 - ph * 10 + d * 0.45, 5, seed + 2),
                              h     = 1 - d / rim + 0.26 * swirl + 0.1 * (1 - back);
                 if (rim - d < 1 && back < 0.3) {
                     out.c = h > 0.3 ? H[3] : H[5];
                     return;
                 }
                 out.c = h > 0.74 ? H[0] : h > 0.58 ? H[1] : h > 0.42 ? H[2] : h > 0.27 ? H[4] : h > 0.13 ? H[5] : H[6];
             });
}
}  // namespace
void fireball(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto                    S = o.seed;
    const double                  R = o.fire.radius, V = o.fire.speed;
    const fire::Noise             N{1.2, t};
    const uint32_t                GLOW = mix(mix(H[3], H[4], 0.5), WHITE, 0.25), SMOKE = mix(hex(0x5b5566), H[6], 0.3);
    const std::array<uint32_t, 4> SPARKS{H[1], H[2], H[4], H[5]}, EMB{H[1], H[2], H[4], H[6]};
    const double                  hx = N(3, S + 50) * 0.5, hy = N(2.5, S + 51) * 0.7;
    struct Puff {
        double x, y, v, vy, fl;
        bool   sm;
        double sl, s, f, life;
    };
    struct Entry {
        double age, x, y;
        Puff   q;
    };
    std::vector<Entry> flames, smoke;
    fire::looped(
        t, 1.2, 130, S + 100,
        [&](Rng& r, int) {
            const double fl = r.range(0.22, 0.55);
            const bool   sm = r.isChance(0.55);
            const double x  = -r.range(2, 24);
            const double y = r.range(-1, 1) * (R - 0.1 * -x), v = V * r.range(0.3, 0.6), vy = r.range(-16, 16),
                         sl = r.range(0.25, 0.45), s = r.range(1.3, 2.9), f = r.next() * 0.5 + 0.5 * (-x / 24);
            return Puff{x, y, v, vy, fl, sm, sl, s, f, fl + (sm ? 0.45 : 0)};
        },
        [&](const Puff& q, double age, int) {
            const double ta = std::min(age, q.fl);
            const auto   d  = fire::drift(0, q.y, q.v, q.vy, ta, 0, -70, 4.5, 3);
            const double x  = q.x + d.x - V * ta;
            if (age < q.fl) {
                flames.push_back({age, x, d.y, q});
                return;
            }
            if (!q.sm || age - q.fl > q.sl) return;
            const double sa = age - q.fl;
            const auto   m  = fire::drift(x, d.y, d.vx - V, d.vy * 0.6 - 6, sa, V * 2, -30, 2, 1.2);
            smoke.push_back({sa, m.x, m.y, q});
        });
    for (const auto& q : smoke) fire::smokePuff(p, q.x, q.y, 1 - q.age / q.q.sl, q.q.s * 0.9, SMOKE);
    std::stable_sort(flames.begin(), flames.end(), [](const Entry& a, const Entry& b) { return a.age > b.age; });
    for (const auto& q : flames) {
        const double u = q.age / q.q.fl;
        fire::firePuff(p, q.x, q.y, 1 - u, q.q.s * (1 + 1.3 * u), q.q.f, H, 0.07, -0.86, -0.5);
    }
    for (size_t i = 0; i < BODY.size(); ++i) {
        const auto&  b = BODY[i];
        const auto   n = (S * 5 + uint32_t(i)) * 17;
        const double L = b[0] * (0.82 + 0.25 * N(4, n)) * (1 + 0.3 * std::max(0.0, N(2.4, n + 3)));
        fire::flameBlade(p, hx + 1, hy + b[2] * 0.5, -1, b[2] * 0.02, L, b[1] * R / 5, N(3, n + 5) * 3 + b[2], N, n, H,
                         fire::LAYERS, 0.5, 1.1 + L * 0.05);
    }
    for (size_t i = 0; i < CORONA.size(); ++i) {
        const auto&  c = CORONA[i];
        const auto   n = (S * 5 + uint32_t(i) + 20) * 17;
        const double a = c[0], L = c[1] * (0.7 + 0.4 * (0.5 + 0.5 * N(6, n))), ox = std::cos(a) * R * 0.85,
                     oy = std::sin(a) * R * 0.85, dx = std::cos(a) * 0.55 - 1, dy = std::sin(a) * 0.55,
                     l = jsHypot2(dx, dy);
        fire::flameBlade(p, hx + ox, hy + oy, dx / l, dy / l, L, c[2], -(a > 0 ? 1.0 : -1.0) * 1.5, N, n, H);
    }
    head(p, H, t, R, S, hx, hy);
    struct Mote {
        double a, v, life;
    };
    fire::looped(
        t, 1.2, 26, S + 200,
        [](Rng& r, int) {
            const double a = r.range(-2.4, 2.4), v = r.range(30, 70), life = r.range(0.08, 0.18);
            return Mote{a, v, life};
        },
        [&](const Mote& q, double age, int) {
            const double ca = std::cos(q.a), sa = std::sin(q.a), d = R * 0.9 + q.v * age,
                         x = hx + ca * d - V * 0.4 * age, y = hy + sa * d, k = 1 - age / q.life, L = 1 + 2 * k;
            p.line(x - ca * L + V * 0.02, y - sa * L, x, y, k > 0.5 ? H[1] : H[3]);
            p.glow(x, y, 3, H[2], 0.4 * k);
        });
    struct Ember {
        double x, y, v, vy, life;
        bool   big;
        double ph;
    };
    fire::looped(
        t, 1.2, 30, S + 300,
        [&](Rng& r, int) {
            const double x = -r.range(2, 16), y = r.range(-3, 3), v = V * r.range(0.3, 0.8), vy = r.range(-20, 20),
                         life = r.range(0.25, 0.55);
            const bool   big  = r.isChance(0.25);
            const double ph   = r.range(0, 6.3);
            return Ember{x, y, v, vy, life, big, ph};
        },
        [&](const Ember& q, double age, int) {
            const auto   d = fire::drift(0, q.y, q.v, q.vy, age, 0, 50, 3, 2);
            const double x = q.x + d.x - V * age, k = 1 - age / q.life;
            if (q.big)
                fire::ember(p, x, d.y, k, 2, EMB, age * 40 + q.ph);
            else
                fire::spark(p, x, d.y, d.vx - V, d.vy, k, SPARKS);
        });
    const double fl = 0.85 + 0.15 * N(9, S + 7);
    p.glowLine(hx - 6, hy, -50, -2, 12, GLOW, 0.2 * fl);
    p.glow(hx - 2, hy, 42, GLOW, 0.42 * fl);
    p.glow(hx, hy, 13, H[1], 0.3 * fl);
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& fireballDescriptor() {
    static const std::array<Option, 2> options{{{"radius", 1, 20, false, 0, &PixelFxFireSettings::radius},
                                                {"speed", 1, 500, false, 0, &PixelFxFireSettings::speed}}};
    static const Descriptor            descriptor{{PixelFxKind::Fireball, "fire/fireball", 136, 56, 112, 28, 1.2, true},
                                                  9,
                                                  [](PixelFxSettings& s) { fireDefaults(s, PixelFxKind::Fireball); },
                                                  &fireball,
                                                  options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
