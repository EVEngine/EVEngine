// eve-hot-path
// Adapted from pixelfx fire/spirit-flame.js, MIT. Copyright (c) 2026 Patch and Hugo Duprez.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Fire.h"
#include "stylize/pixelfx/PixelFxInternal.h"

namespace eve::stylize::pixelfx {
namespace {
constexpr std::array<fire::Layer, 5> GHOST{
    {{1, 1, 1}, {0.8, 0.92, 3}, {0.6, 0.78, 6}, {0.3, 0.42, 2}, {0.14, 0.22, 0}}};
template <class Centre>
void ghostTongue(Paint& p, double x, double y, double h, double w, Centre centre, const fire::Noise& N, uint32_t n,
                 const std::array<uint32_t, 9>& H, double jit = 0.5) {
    for (const auto& layer : GHOST) {
        const double L = h * layer.height;
        for (int r = 0; r <= L; ++r) {
            const double u = r / std::max(1.0, L), v = r / h;
            const double half =
                w * layer.width * std::pow(1 - u, 0.75) * std::min(1.0, (r + 1.5) / 4) + N(6, n, r * 0.4) * jit * u;
            if (half < 0.3) continue;
            const double cx = x + centre(v, double(r)), Y = jsRound(y - r);
            for (int px = int(jsRound(cx - half)), end = int(jsRound(cx + half)); px <= end; ++px)
                p.px(px, Y, H[size_t(layer.ramp)]);
        }
    }
}
}  // namespace
void spiritFlame(Paint& p, double t, const PixelFxSettings& o) {
    std::array<uint32_t, 9> H{};
    std::reverse_copy(o.palette.begin(), o.palette.end(), H.begin());
    const auto        S  = o.seed;
    const double      ph = t / 3;
    const fire::Noise N{3, t};
    const uint32_t    COLD = mix(H[3], WHITE, 0.15);
    const double      bob = std::sin(ph * TAU) * 1.5, lean = N(0.67, S + 2) * 3;
    const auto        wave = [&](double v, double r, double k = 0) {
        return (std::sin(r * 0.24 - ph * TAU * 2 + k) * 2.2 + std::sin(r * 0.11 - ph * TAU + k * 0.5) * 3.2) * v * v +
               lean * v * v;
    };
    const double H0 = 44 * (0.92 + 0.08 * N(1.3, S + 3)), y0 = bob, br = 0.9 + 0.1 * N(2, S + 4);
    p.glow(0, y0 - 16, 60, COLD, 0.36 * br);
    p.glow(wave(0.5, 20) * 0.5, y0 - 20, 26, H[2], 0.2 * br);
    struct Curl {
        double x, y, vx, vy, life, s, a, w;
    };
    fire::looped(
        t, 3, 40, S + 300,
        [&](Rng& r, int) {
            const double x = r.range(-4, 4), y = -H0 * r.range(0.55, 0.95), vx = r.range(-7, 7), vy = -r.range(10, 22),
                         life = r.range(1, 1.9), s = r.range(1.4, 2.8), a = r.range(0, TAU), w = r.range(2, 3.5);
            return Curl{x, y, vx, vy, life, s, a, w};
        },
        [&](const Curl& q, double age, int) {
            const auto   d = fire::drift(q.x + wave(0.9, 26) * 0.5, q.y + y0, q.vx, q.vy, age, 0, -6, 1.2, 0.6);
            const double k = 1 - age / q.life, cx = d.x + std::sin(age * q.w + q.a) * 3 * (age + 0.2),
                         a = std::min(1.0, age * 5) * k;
            fire::soft(p, cx, d.y, q.s * (0.8 + age * 0.8), H[4], 0.35 * a);
            fire::soft(p, cx - 0.4, d.y - 0.6, q.s * 0.5 * (0.8 + age * 0.5), H[2], 0.4 * a * k);
            p.glow(cx, d.y, 6, H[3], 0.15 * a);
        });
    const double wl = std::max(0.0, 0.25 + 0.55 * N(1.7, S + 6)), wr = std::max(0.0, 0.25 + 0.55 * N(1.7, S + 7));
    if (wl > 0.12)
        ghostTongue(
            p, -4, y0 - 4, H0 * wl, 2.4,
            [&](double v, double r) { return wave(v, r, 2.1) - 6 * v * v + 3 * std::sin(v * 5 - ph * TAU * 2) * v; }, N,
            S * 11 + 1, H);
    if (wr > 0.12)
        ghostTongue(
            p, 4.2, y0 - 4, H0 * wr, 2.4,
            [&](double v, double r) {
                return wave(v, r, -1.8) + 6 * v * v - 3 * std::sin(v * 5 - ph * TAU * 2 + 1) * v;
            },
            N, S * 11 + 2, H);
    ghostTongue(p, 0, y0, H0, 8.6, [&](double v, double r) { return wave(v, r); }, N, S * 11, H, 0.6);
    for (int k : {0, 1})
        for (int r = 4; r < H0 * 0.72; ++r) {
            const double v = r / H0, x = wave(v, r) + std::sin(r * 0.35 - ph * TAU * 3 + k * PI) * 2.2 * (1 - v);
            if (std::sin(r * 0.2 - ph * TAU * 4 + k * 2) > 0.2) p.px(x, y0 - r, H[k ? 2 : 1]);
        }
    for (double v : {0.2, 0.45, 0.7}) p.glow(wave(v, H0 * v), y0 - H0 * v, 12, H[2], 0.16 * br);
    p.glow(0, y0 - 4, 9, H[1], 0.22 + 0.06 * N(3, S + 5));
    struct Spark {
        double x, y, vx, vy, life, a;
    };
    fire::looped(
        t, 3, 34, S + 400,
        [](Rng& r, int) {
            const double x = r.range(-7, 7), y = -r.range(4, 28), vx = r.range(-12, 12), vy = -r.range(14, 34),
                         life = r.range(0.9, 1.8), a = r.range(0, TAU);
            return Spark{x, y, vx, vy, life, a};
        },
        [&](const Spark& q, double age, int) {
            const auto   d = fire::drift(q.x, q.y + y0, q.vx, q.vy, age, 0, -10, 1.2, 1.2);
            const double k = 1 - age / q.life, tw = 0.5 + 0.5 * std::sin(age * 14 + q.a),
                         sx = d.x + std::sin(age * 3 + q.a) * 3;
            if (k * (0.4 + 0.6 * tw) < 0.2) return;
            p.px(sx, d.y, tw > 0.7 && k > 0.5 ? H[0] : k > 0.5 ? H[1] : H[3]);
            if (tw > 0.85 && k > 0.6) {
                p.px(sx - 1, d.y, H[3]);
                p.px(sx + 1, d.y, H[3]);
                p.px(sx, d.y - 1, H[3]);
                p.px(sx, d.y + 1, H[3]);
            }
            p.glow(sx, d.y, 4, H[2], 0.3 * k);
        });
}
}  // namespace eve::stylize::pixelfx

namespace eve::stylize::pixelfx {

const Descriptor& spiritFlameDescriptor() {
    static const Descriptor descriptor{{PixelFxKind::SpiritFlame, "fire/spirit-flame", 80, 124, 40, 94, 3, true},
                                       9,
                                       [](PixelFxSettings& s) { fireDefaults(s, PixelFxKind::SpiritFlame); },
                                       &spiritFlame};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
