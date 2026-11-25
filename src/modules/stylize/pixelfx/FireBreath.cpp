// eve-hot-path
// Adapted from pixelfx fire/fire-breath.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void fireBreath(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto                    S = o.seed;
    const double                  A = o.fire.spread, V = o.fire.speed;
    const fire::Noise             N{1.5, t};
    const uint32_t                GLOW = mix(mix(H[3], H[4], 0.5), WHITE, 0.25), SMOKE = mix(hex(0x5b5566), H[6], 0.3);
    const std::array<uint32_t, 4> SPARKS{H[1], H[2], H[4], H[5]}, EMB{H[1], H[2], H[4], H[6]};
    const auto                    roar = [&](double tt) { return 0.85 + 0.15 * N.at(tt, 3, S + 1); };
    const auto                    sway = [&](double tt) { return N.at(tt, 1.3, S + 2) * 0.05; };
    struct Puff {
        double e, sp, fl;
        bool   sm;
        double sl, s, f, ox, life;
    };
    struct Entry {
        double age, x, y;
        Puff   q;
    };
    std::vector<Entry> flames, smoke;
    fire::looped(
        t, 1.5, 260, S + 100,
        [](Rng& r, int) {
            const double e = r.range(-1, 1), fl = r.range(0.42, 0.8) * (1 - 0.3 * std::abs(e));
            const bool   sm = r.isChance(0.2 + 0.4 * std::abs(e));
            const double sp = r.range(0.75, 1.1), sl = r.range(0.5, 0.85), s = r.range(3, 5.2), f = r.next(),
                         ox = r.range(4, 10);
            return Puff{e, sp, fl, sm, sl, s, f, ox, fl + (sm ? 0.85 : 0)};
        },
        [&](const Puff& q, double age, int) {
            const double tb = t - age, a = q.e * A + sway(tb) - 0.04, v = V * q.sp * roar(tb), ta = std::min(age, q.fl);
            const auto   d = fire::drift(q.ox, q.e * 1.5, std::cos(a) * v, std::sin(a) * v, ta, 0, -100, 2, 2.4);
            if (age < q.fl) {
                flames.push_back({age, d.x, d.y, q});
                return;
            }
            if (!q.sm || age - q.fl > q.sl) return;
            const double sa = age - q.fl;
            const auto   m  = fire::drift(d.x, d.y, d.vx * 0.5, d.vy * 0.6 - 8, sa, 0, -26, 1.8, 1.2);
            smoke.push_back({sa, m.x, m.y, q});
        });
    for (const auto& e : smoke) fire::smokePuff(p, e.x, e.y, 1 - e.age / e.q.sl, e.q.s * 1.1, SMOKE, true, 0.7);
    std::stable_sort(flames.begin(), flames.end(), [](const Entry& a, const Entry& b) { return a.age > b.age; });
    constexpr std::array<fire::Layer, 3> COOL{{{1, 1, 7}, {0.72, 0.82, 6}, {0.44, 0.58, 5}}};
    for (const auto& e : flames) {
        const auto&  q = e.q;
        const double u = e.age / q.fl, edge = std::abs(q.e), size = q.s * (0.75 + 1.6 * u),
                     heat = std::min(0.999, std::pow(u, 1.5) * (0.85 + 0.2 * edge) + 0.3 * edge * edge),
                     tear = clamp((u - 0.32) / 0.2);
        fire::firePuff(p, e.x, e.y, 1 - heat, size * (1 - 0.55 * tear), q.f, H, 0.1, -0.5, -0.86);
        if (tear > 0) {
            const auto                         n      = uint32_t(std::floor(q.f * 1e5));
            const double                       h      = size * (1.5 + 0.5 * N(4, n)) * (1.2 - heat * 0.6) * tear;
            const std::span<const fire::Layer> layers = heat < 0.45   ? std::span<const fire::Layer>(fire::LAYERS)
                                                        : heat < 0.72 ? std::span<const fire::Layer>(fire::BACK_LAYERS)
                                                                      : std::span<const fire::Layer>(COOL);
            fire::flameTongue(p, e.x, e.y + size * 0.3, h, size * 0.5 * (1.1 - heat * 0.5),
                              -h * 0.4 + N(2.5, n + 1) * 2, N, n, H, layers);
        }
    }
    for (int i = 0; i < 6; ++i) {
        const auto   n    = (S * 3 + uint32_t(i)) * 19;
        const double side = i % 2 ? 1 : -1, d = 18 + (i >> 1) * 16 + N(2, n) * 4,
                     L = (9 + 5 * (0.5 + 0.5 * N(5, n + 1))) * (1 - d / 120);
        fire::flameBlade(p, d, side * d * A * 0.85 - d * 0.06, 0.75, side * 0.45 - 0.48, L, 1.6 + L * 0.08, -side * 3,
                         N, n, H);
    }
    struct Streak {
        double e, v, life, l;
    };
    fire::looped(
        t, 1.5, 70, S + 250,
        [](Rng& r, int) {
            const double e = r.range(-0.6, 0.6), v = r.range(330, 470), life = r.range(0.12, 0.26), l = r.range(4, 9);
            return Streak{e, v, life, l};
        },
        [&](const Streak& q, double age, int) {
            const double a  = q.e * A - 0.03;
            const auto   d  = fire::drift(6, q.e * 2, std::cos(a) * q.v, std::sin(a) * q.v, age, 0, -60, 2, 2);
            const double sp = jsHypot2(d.vx, d.vy), k = 1 - age / q.life, L = q.l * (sp / 400);
            p.lineFn(
                d.x - d.vx / sp * L, d.y - d.vy / sp * L, d.x, d.y,
                [&](double u) { return u > 0.6   ? (k > 0.5 ? H[0] : H[1])
                                       : k > 0.5 ? H[2]
                                                 : H[3]; }, 1.0);
        });
    constexpr std::array<std::array<double, 3>, 4> JET{{{42, 4.2, 0}, {32, 3.8, -0.08}, {30, 3.8, 0.08}, {20, 4.6, 0}}};
    for (size_t i = 0; i < JET.size(); ++i) {
        const auto&  q = JET[i];
        const auto   n = (S * 5 + uint32_t(i)) * 17;
        const double L = q[0] * roar(t) * (0.85 + 0.2 * N(5, n)), aa = q[2] + sway(t);
        fire::flameBlade(p, 0, 0, std::cos(aa), std::sin(aa), L, q[1], N(3, n + 3) * 3, N, n, H, fire::LAYERS, 0.5,
                         1.5);
    }
    struct Spark {
        double d, e, v, life;
    };
    fire::looped(
        t, 1.5, 40, S + 300,
        [](Rng& r, int) {
            const double d = r.range(4, 60), e = r.range(-1, 1), v = r.range(140, 280), life = r.range(0.25, 0.55);
            return Spark{d, e, v, life};
        },
        [&](const Spark& q, double age, int) {
            const double a = q.e * A * 1.8 - 0.12;
            const auto   d =
                fire::drift(q.d, q.e * q.d * A, std::cos(a) * q.v, std::sin(a) * q.v - 30, age, 0, 190, 2, 2);
            fire::spark(p, d.x, d.y, d.vx, d.vy, 1 - age / q.life, SPARKS);
        });
    struct Ember {
        double x, y, vx, vy, life, s, ph;
    };
    fire::looped(
        t, 1.5, 18, S + 400,
        [](Rng& r, int) {
            const double x = r.range(50, 100), y = r.range(-14, 6), vx = r.range(10, 40), vy = -r.range(20, 50),
                         life = r.range(0.8, 1.4), s = r.isChance(0.2) ? 2 : 1, ph = r.range(0, 6.3);
            return Ember{x, y, vx, vy, life, s, ph};
        },
        [&](const Ember& q, double age, int) {
            const auto d = fire::drift(q.x, q.y, q.vx, q.vy, age, 0, -30, 1.6, 1.6);
            fire::ember(p, d.x + 5 * (std::sin(age * 2.3 + q.ph) - std::sin(q.ph)), d.y, 1 - age / q.life, q.s, EMB,
                        age * 40 + q.ph);
        });
    const double k = roar(t);
    p.glow(3, 0, 34 * k, GLOW, 0.75 * k);
    p.glow(3, 0, 10, H[0], 0.55);
    for (int d = 10; d < 126; d += 8) p.glow(d, -d * 0.1, 10 + d * 0.2, GLOW, 0.32 * k * (1 - double(d) / 150));
    p.glow(64, -12, 96, GLOW, 0.16 * k);
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& fireBreathDescriptor() {
    static const std::array<Option, 2> options{{{"spread", 0, 1, false, 0, &PixelFxFireSettings::spread},
                                                {"speed", 1, 500, false, 0, &PixelFxFireSettings::speed}}};
    static const Descriptor descriptor{{PixelFxKind::FireBreath, "fire/fire-breath", 160, 116, 12, 72, 1.5, true},
                                       9,
                                       [](PixelFxSettings& s) { fireDefaults(s, PixelFxKind::FireBreath); },
                                       &fireBreath,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
