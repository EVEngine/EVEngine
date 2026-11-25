// eve-hot-path
// Adapted from pixelfx fire/fire-tornado.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
void fireTornado(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto                    S  = o.seed;
    const double                  HT = o.fire.height, ph = t / 2;
    const fire::Noise             N{2, t};
    const uint32_t                GLOW = mix(mix(H[3], H[4], 0.5), WHITE, 0.25), SMOKE = mix(hex(0x5b5566), H[6], 0.25);
    const std::array<uint32_t, 4> EMB{H[1], H[2], H[4], H[6]};
    const auto                    cx = [](double v, double tt) {
        return 3.2 * std::sin(v * 4.2 - tt / 2 * TAU * 2) * v + 2 * std::sin(tt / 2 * TAU) * v;
    };
    const auto rad = [](double v) { return 3.5 + 15 * std::pow(v, 1.35); };
    struct Orbit {
        double x, y, z, k, vx, vy;
    };
    std::vector<Orbit> orbit;
    struct OrbitSeed {
        double v0, rise, a, w, out, life;
        bool   big;
    };
    fire::looped(
        t, 2, 30, S + 500,
        [](Rng& r, int) {
            const double v0 = r.range(0.02, 0.7), rise = r.range(0.12, 0.3), a = r.range(0, TAU), w = r.range(4, 7),
                         out = r.range(4, 12), life = r.range(1.2, 2.4);
            const bool   big = r.isChance(0.2);
            return OrbitSeed{v0, rise, a, w, out, life, big};
        },
        [&](const OrbitSeed& q, double age, int) {
            const double v = q.v0 + q.rise * age, R = rad(std::min(1.0, v)) + q.out + age * 4, a = q.a + q.w * age,
                         k = 1 - age / q.life;
            orbit.push_back({cx(std::min(1.0, v), t) + std::cos(a) * R, -v * HT + std::sin(a) * R * 0.28, std::sin(a),
                             k, -std::sin(a) * R * q.w, std::cos(a) * R * q.w * 0.28});
        });
    const auto embers = [&](bool front) {
        for (const auto& e : orbit) {
            if ((e.z >= 0) != front || e.k <= 0) continue;
            const double speed = jsHypot2(e.vx, e.vy), sp = speed == 0 ? 1 : speed, L = std::min(5.0, 1 + sp * 0.025);
            const auto   c = e.k > 0.6 ? EMB[0] : e.k > 0.3 ? EMB[1] : EMB[2];
            p.lineFn(
                e.x - e.vx / sp * L, e.y - e.vy / sp * L, e.x, e.y,
                [&](double u) { return u > 0.6 ? c
                                       : front ? EMB[3]
                                               : H[7]; }, 1.0);
            p.glow(e.x, e.y, front ? 4 : 3, c, (front ? 0.4 : 0.15) * e.k);
        }
    };
    const auto ring = [&](bool front) {
        for (int i = 0; i < 12; ++i) {
            const double a = double(i) / 12 * TAU + t / 2 * TAU * 2, z = std::sin(a);
            if ((z >= 0) != front) continue;
            const auto   n = (S * 3 + uint32_t(i)) * 19;
            const double R = 12 + 2 * N(2, n), x = std::cos(a) * R, y = z * R * 0.25,
                         h = (5 + 5 * (0.5 + 0.5 * N(5, n + 1))) * (front ? 1 : 0.8);
            fire::flameTongue(p, x, y, h, 1.6, -std::cos(a + PI / 2) * 3, N, n, H,
                              front ? fire::LAYERS : fire::BACK_LAYERS);
        }
    };
    struct Smoke {
        double a, life, s, vy;
    };
    fire::looped(
        t, 2, 20, S + 300,
        [](Rng& r, int) {
            const double a = r.range(0, TAU), life = r.range(1.2, 2), s = r.range(2.5, 4), vy = -r.range(12, 22);
            return Smoke{a, life, s, vy};
        },
        [&](const Smoke& q, double age, int) {
            const double R = rad(1) * (0.8 + age * 0.5), a = q.a + age * 3, x = cx(1, t - age) + std::cos(a) * R,
                         y = -HT - 2 + q.vy * age + std::sin(a) * R * 0.25;
            fire::smokePuff(p, x, y, 1 - age / q.life, q.s, SMOKE);
        });
    fire::floorLight(p, 0, 0.5, 40, 8, GLOW, 0.4);
    embers(false);
    ring(false);
    const double fl = 0.85 + 0.15 * N(7, S + 1);
    for (int y = 0; y >= -HT * 1.2; --y) {
        const double v = -y / HT, vv = std::min(1.0, v), c = cx(vv, t), r = rad(vv) * (1 + 0.08 * N(3, S + 2, y * 0.1)),
                     sp    = t / 2 * TAU * 3 + v * 5.5;
        const double cut   = clamp((v - 0.72) / 0.42),
                     lickL = 0.3 * std::max(0.0, fire::pnoise(-y * 0.22 + ph * 18, 18, S + 3)),
                     lickR = 0.3 * std::max(0.0, fire::pnoise(-y * 0.22 + ph * 18 + 9, 18, S + 4));
        for (int x = int(std::floor(c - r * 1.35)); x <= std::ceil(c + r * 1.35); ++x) {
            const double dx = (x + 0.5 - c) / r, edge = 1 + (dx < 0 ? lickL : lickR);
            if (std::abs(dx) > edge) continue;
            const double th = std::asin(clamp(dx, -1, 1)), depth = std::cos(th),
                         tear = 0.18 * fire::pnoise(y * 0.45 + dx * 2 - ph * 30, 30, S + 6);
            const double band = 0.5 + 0.5 * std::sin((th + sp + tear) * 3), streak = band * band * band;
            if (cut > 0 && band < 0.35 + cut * 0.6 + 0.1 * fire::pnoise(x * 0.7 + ph * 20, 20, S + 5)) continue;
            double heat = 0.22 + 0.36 * depth + 0.5 * streak - 0.22 * vv + 0.1 * (1 - vv) - 0.25 * cut;
            if (std::abs(dx) > 1) heat = 0.3 - (std::abs(dx) - 1) * 1.5;
            heat *= fl;
            p.px(x, y,
                 heat > 0.95   ? H[0]
                 : heat > 0.82 ? H[1]
                 : heat > 0.7  ? H[2]
                 : heat > 0.58 ? H[3]
                 : heat > 0.46 ? H[4]
                 : heat > 0.34 ? H[5]
                 : heat > 0.2  ? H[6]
                               : H[7]);
        }
        if (y % 10 == 0 && v <= 1) p.glow(c, y, r * 2 + 8, GLOW, 0.22 * fl);
    }
    struct Side {
        double v, side, sp, life, s, f;
    };
    fire::looped(
        t, 2, 70, S + 200,
        [](Rng& r, int) {
            const double v = r.range(0.05, 0.95), side = r.sign(), sp = r.range(20, 50), life = r.range(0.25, 0.5),
                         s = r.range(1.4, 2.8), f = r.next();
            return Side{v, side, sp, life, s, f};
        },
        [&](const Side& q, double age, int) {
            const double x0 = cx(q.v, t - age) + q.side * rad(q.v), y0 = -q.v * HT;
            const auto   d = fire::drift(x0, y0, q.side * q.sp, -q.sp * 0.6, age, 0, -60, 3, 3);
            fire::firePuff(p, d.x, d.y, 1 - age / q.life, q.s, q.f, H, 0.1, -q.side * 0.6, -0.8);
        });
    ring(true);
    embers(true);
    p.glow(0, -HT * 0.5, 80, GLOW, 0.28 * fl);
    p.glow(0, -6, 18, H[2], 0.3 * fl);
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& fireTornadoDescriptor() {
    static const std::array<Option, 1> options{{{"height", 1, 100, false, 0, &PixelFxFireSettings::height}}};
    static const Descriptor descriptor{{PixelFxKind::FireTornado, "fire/fire-tornado", 120, 152, 60, 136, 2, true},
                                       9,
                                       [](PixelFxSettings& s) {
                                           fireDefaults(s, PixelFxKind::FireTornado);
                                           s.fire.height = 80;
                                       },
                                       &fireTornado,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
