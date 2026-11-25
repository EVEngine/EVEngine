// eve-hot-path
// Adapted from pixelfx fire/flamethrower.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
double flow(double t) {
    const double u = wrap(t, 1.6), q = (u - 0.55 - 0.2) / 0.05;
    return 1 - smooth(0.55, 0.55 + 0.04, u) * (1 - smooth(0.55 + 0.1, 0.55 + 0.16, u)) + 0.25 * std::exp(-(q * q));
}
}  // namespace
void flamethrower(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto&                   PL = o.fire.pilot;
    const auto                    S  = o.seed;
    const double                  L = o.fire.reach, V = o.fire.speed;
    const fire::Noise             N{1.6, t};
    const uint32_t                GLOW = mix(mix(H[3], H[4], 0.5), WHITE, 0.25), SMOKE = mix(hex(0x2a2430), H[7], 0.3);
    const std::array<uint32_t, 4> SPARKS{H[1], H[2], H[4], H[5]};
    const auto                    cy = [&](double x) {
        return 5 * std::pow(x / L, 2) + N(1.2, S + 9, x * 0.02) * 2.2 * std::pow(x / L, 1.5);
    };
    const auto width = [&](double x) { return (2.1 + 5 * std::pow(x / L, 0.7)) * flow(t - x / V); };
    struct Curl {
        double a0, R, w, up, dx, fl, sl, s, f, life;
    };
    struct Entry {
        double u, x, y;
        Curl   q;
        double k;
    };
    std::vector<Entry> flames, smoke;
    fire::looped(
        t, 1.6, 80, S + 100,
        [](Rng& r, int) {
            const double a0 = r.range(-0.3, 0.5), R = r.range(9, 15), w = r.range(5, 7.5), up = r.range(4, 12),
                         dx = r.range(-6, 2), fl = r.range(0.38, 0.62), sl = r.range(0.6, 1), s = r.range(2.4, 4.4),
                         f = r.next();
            return Curl{a0, R, w, up, dx, fl, sl, s, f, 1.6};
        },
        [&](const Curl& q, double age, int) {
            const double k = flow(t - age - L / V);
            if (k < 0.4) return;
            const double x0 = L + q.dx, y0 = cy(L), a = std::min(age, q.fl) * q.w + q.a0, x = x0 + q.R * std::sin(a),
                         y = y0 - q.R * (1 - std::cos(a)) - q.up * age;
            if (age < q.fl) {
                flames.push_back({age / q.fl, x, y, q, k});
                return;
            }
            if (age - q.fl < q.sl)
                smoke.push_back({(age - q.fl) / q.sl, x - (age - q.fl) * 6, y - (age - q.fl) * 16, q, k});
        });
    for (const auto& e : smoke) fire::smokePuff(p, e.x, e.y, 1 - e.u, e.q.s * 1.2, SMOKE, true, 0.85);
    std::stable_sort(flames.begin(), flames.end(), [](const Entry& a, const Entry& b) { return a.u > b.u; });
    for (const auto& e : flames) fire::firePuff(p, e.x, e.y, 1 - e.u, e.q.s * (0.8 + 1.1 * e.u) * e.k, e.q.f, H, 0.1);
    for (int i = 0; i < 13; ++i) {
        const auto   n  = (S * 3 + uint32_t(i)) * 19;
        const double ph = std::fmod(i * 0.618 + t / 1.6, 1), x = 10 + ph * (L - 14), k = flow(t - x / V);
        if (k < 0.35) continue;
        const double h =
            (2 + 11 * (x / L)) * (0.55 + 0.6 * (0.5 + 0.5 * N(5, n + 1))) * k * std::min(1.0, (1 - ph) * 6);
        fire::flameTongue(p, x, cy(x) - width(x) * 0.6, h, 1.1 + 1.6 * (x / L), -h * 0.55, N, n, H);
    }
    const double P = jsRound(1.6 * 3);
    for (int x = 0; x <= L; ++x) {
        const double c = cy(x), w = width(x) * (1 + 0.15 * fire::pnoise(x * 0.4 - t / 1.6 * 24, 24, S + 4));
        if (w < 0.4) continue;
        const int                     surge = wrap(x * 0.035 - t / 1.6 * P, 1) < 0.12 ? 1 : 0;
        const std::array<uint32_t, 5> cols{H[0], H[2], H[4], H[6], H[7]};
        for (int y = int(std::floor(c - w - 1)); y <= std::ceil(c + w + 1); ++y) {
            const double q = std::abs(y + 0.5 - c) / w;
            if (q > 1.15) continue;
            int lvl = (q < 0.3 ? 0 : q < 0.55 ? 1 : q < 0.78 ? 2 : q < 1 ? 3 : 4) + (x > L * 0.45 && q < 0.55 ? 1 : 0);
            lvl     = std::clamp(lvl - surge, 0, 4);
            if (lvl == 4 && fire::pnoise(x * 0.5 + y * 7.3 - t / 1.6 * 40, 40, S + 6) < 0) continue;
            p.px(x, y, cols[size_t(lvl)]);
        }
        for (double ph : {0.0, PI}) {
            const double a = x * 0.22 - t / 1.6 * PI * 2 * 9 + ph, sy = c + std::sin(a) * w * 0.55;
            if (std::cos(a) > -0.1 && w > 1.2) p.px(x, sy, std::cos(a) > 0.55 ? H[0] : H[1]);
        }
    }
    const double pk = flow(t);
    for (int x = 0; x < 6; ++x)
        for (int y = -2; y <= 2; ++y)
            if (std::abs(y) <= 2 - x * 0.3) p.px(x, y, std::abs(y) < 1 ? (x < 2 ? PL[2] : PL[1]) : PL[0]);
    fire::solid(p, 1, 0, 1.6 * pk, WHITE);
    struct Drop {
        double d, vx, vy, life;
    };
    fire::looped(
        t, 1.6, 10, S + 200,
        [](Rng& r, int) {
            const double d = r.range(20, 95), vx = r.range(0.3, 0.6), vy = r.range(-25, 15), life = r.range(0.35, 0.6);
            return Drop{d, vx, vy, life};
        },
        [&](const Drop& q, double age, int) {
            if (flow(t - age - q.d / V) < 0.5) return;
            const auto at = [&](double a) { return fire::drift(q.d, cy(q.d), V * q.vx, q.vy, a, 0, 260, 1.2, 0.5); };
            for (int j = 4; j >= 1; --j) {
                const double a = age - j * 0.035;
                if (a < 0) continue;
                const auto d = at(a);
                fire::firePuff(p, d.x, d.y, 1 - std::min(0.99, j * 0.18 + age / q.life * 0.4), 1.8 - j * 0.25, 0.3, H,
                               0.06);
            }
            const auto   d = at(age);
            const double k = 1 - age / q.life;
            p.px(d.x, d.y, k > 0.5 ? H[0] : H[2]);
            p.px(d.x + 1, d.y, H[3]);
            p.glow(d.x, d.y, 6, H[3], 0.45 * k);
        });
    struct Sputter {
        double born, a, v, life;
    };
    fire::looped(
        t, 1.6, 12, S + 300,
        [](Rng& r, int) {
            const double born = 0.55 + r.range(0, 0.08), a = r.range(-0.8, 0.8), v = r.range(120, 240),
                         life = r.range(0.2, 0.45);
            return Sputter{born, a, v, life};
        },
        [&](const Sputter& q, double age, int) {
            const double u = wrap(t - age, 1.6);
            if (std::abs(u - q.born) > 0.06) return;
            const auto d = fire::drift(4, 0, std::cos(q.a) * q.v, std::sin(q.a) * q.v, age, 0, 300, 2.5, 2.5);
            fire::spark(p, d.x, d.y, d.vx, d.vy, 1 - age / q.life, SPARKS);
        });
    struct Blob {
        double born, v, vy, life, s;
    };
    fire::looped(
        t, 1.6, 5, S + 350,
        [](Rng& r, int) {
            const double born = 0.55 + r.range(0.01, 0.1), v = r.range(90, 170), vy = r.range(-30, 10),
                         life = r.range(0.25, 0.4), s = r.range(1.6, 2.6);
            return Blob{born, v, vy, life, s};
        },
        [&](const Blob& q, double age, int) {
            const double u = wrap(t - age, 1.6);
            if (std::abs(u - q.born) > 0.05) return;
            const auto d = fire::drift(4, 0, q.v, q.vy, age, 0, 200, 2, 1);
            fire::firePuff(p, d.x, d.y, 1 - age / q.life, q.s, 0.2, H, 0.1, -0.86, -0.5);
        });
    const double a = wrap(t, 1.6) - 0.55;
    if (a > 0 && a < 0.7) fire::smokePuff(p, 8 + a * 20, -a * 18, 1 - a / 0.7, 3, SMOKE, false, 0.8);
    p.glow(2, 0, 34 * pk, GLOW, 0.8 * pk);
    p.glow(2, 0, 9, PL[2], 0.45 * pk);
    for (int d = 8; d <= L; d += 8) p.glow(d, cy(d), 12 + d * 0.16, GLOW, 0.3 * flow(t - d / V));
    p.glow(L, cy(L) - 10, 52, GLOW, 0.34 * flow(t - L / V));
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& flamethrowerDescriptor() {
    static const std::array<Option, 3> options{{{"reach", 20, 200, false, 0, &PixelFxFireSettings::reach},
                                                {"speed", 1, 500, false, 0, &PixelFxFireSettings::speed},
                                                {"pilot", 1, 64, false, 3, &PixelFxFireSettings::pilot}}};
    static const Descriptor descriptor{{PixelFxKind::Flamethrower, "fire/flamethrower", 184, 104, 30, 60, 1.6, true},
                                       9,
                                       [](PixelFxSettings& s) { fireDefaults(s, PixelFxKind::Flamethrower); },
                                       &flamethrower,
                                       options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
