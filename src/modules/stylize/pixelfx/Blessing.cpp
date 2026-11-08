// eve-hot-path
// Native adaptation of pixelfx holy/blessing.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                               TOP = -108, T = 3;
constexpr std::array<std::array<double, 4>, 4> RAYS{
    {{{-.62, .16, 1, .3}}, {{-.2, .2, 2, 1.7}}, {{.22, .14, 1, 4.1}}, {{.6, .18, 2, 2.6}}}};
double halfW(double y) { return lerp(5, 20, (y - TOP) / -TOP); }
double ray(double q, double t) {
    double I = .3 * std::max(0., 1 - q * q);
    for (auto v : RAYS)
        I += std::exp(-std::pow((q - v[0]) / v[1], 2)) * (.55 + .45 * std::sin((t / T) * TAU * v[2] + v[3])) * .6;
    return I * smooth(1.25, .75, std::abs(q));
}
bool inside(const PixelFxActor& A, double x, double y) { return holy::containsActorCell(A, x, y); }
struct Landing {
    double q, v, sway, phase, size, land;
};
std::array<Landing, 20> prepare(const PixelFxSettings& o) {
    std::array<Landing, 20> out{};
    for (uint32_t i = 0; i < 20; ++i) {
        Rng          r(hash({o.seed, i}));
        const double q = r.range(-.9, .9), v = r.range(22, 34), sway = r.range(1, 3), phase = r.range(0, TAU),
                     size = r.range(.8, 2.4);
        double       land = INFINITY;
        for (double age = 0; age < 6; age += .02) {
            const double y = TOP + 6 + v * age, x = q * halfW(y) + std::sin(age * 2.2 + phase) * sway;
            if (y >= -1 || inside(o.actor, x, y)) {
                land = age;
                break;
            }
        }
        out[i] = {q, v, sway, phase, size, land};
    }
    return out;
}
struct Blessing final : AnalyticKernel {
    const std::array<Landing, 20> landings;
    explicit Blessing(const PixelFxSettings& o) : landings(prepare(o)) {}
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        // Source each() transforms bounds and maps cell centers back into local coordinates.
        p.shade(-24, TOP, 24, .5, [&](double x, double y, int sx, int sy) {
            const double I = ray(x / halfW(y), t) * (.6 + .4 * smooth(TOP, 0, y));
            if (I < .02) return;
            p.blendPx(sx, sy, C[6], I * .13);
            p.add(sx, sy, C[4], I * .22);
        });
        for (size_t i : {size_t(1), size_t(2)}) {
            const auto v = RAYS[i];
            for (double y = TOP; y < -1; ++y) {
                const double pulse = .5 + .5 * std::sin(y * .11 - (t / T) * TAU * 3 + v[3]);
                p.dot(
                    v[0] * halfW(y), y, pulse > .85 ? WHITE : C[6],
                    (.1 + .45 * pulse) * (.5 + .5 * std::sin((t / T) * TAU * v[2] + v[3])) * smooth(TOP, TOP + 20, y));
            }
        }
        p.glow(0, -4, 30, C[4], .22);
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& A = o.actor;
        if (A.source != PixelFxActorSource::None) {
            for (int i = 0; i < A.width; ++i)
                for (int j = 0; j < A.height; ++j) {
                    const auto c = A.pixels[size_t(j) * size_t(A.width) + size_t(i)];
                    if (!c || luma(c) < .15) continue;
                    p.px(A.x + i, A.y + j, mix(c, C[6], .35));
                    break;
                }
            p.glow(0, -30, 20, C[5], .22);
        }
        for (int y = 0; y <= 3; ++y)
            for (int x = -24; x <= 24; ++x) {
                const double u = jsHypot2(double(x) / 24, double(y) / 4);
                if (u >= 1) continue;
                p.dot(x, y,
                      u < .3    ? (y ? C[5] : WHITE)
                      : u < .6  ? C[5]
                      : u < .85 ? C[4]
                                : C[3],
                      y == 0 ? std::sqrt(1 - u) : (1 - u) * .7);
            }
        if (A.source != PixelFxActorSource::None)
            cycle(t, T, 20, o.seed, 3., [&](double age, double, Rng&, int i) {
                const auto   s = landings[size_t(i)];
                const double y = TOP + 6 + s.v * age, x = s.q * halfW(y) + std::sin(age * 2.2 + s.phase) * s.sway,
                             fade = std::min(1., age / .3);
                if (age > s.land) {
                    const double w = (age - s.land) / .14;
                    if (w < 1) {
                        const double ly = TOP + 6 + s.v * s.land,
                                     lx = s.q * halfW(ly) + std::sin(s.land * 2.2 + s.phase) * s.sway;
                        p.sparkle(lx, ly, 3 * (1 - w) + 1, WHITE, C[6], .5 * (1 - w));
                    }
                    return;
                }
                const double tw = .6 + .4 * std::sin(age * (7 + (i % 4)) + s.phase);
                if (s.size > 1.2)
                    p.sparkle(x, y, s.size * tw * fade, tw > .85 ? WHITE : C[6], C[5], .35 * fade);
                else {
                    p.dot(x, y, tw > .8 ? WHITE : C[6], fade);
                    p.glow(x, y, 3, C[5], .35 * fade);
                }
            });
        cycle(t, T, 16, o.seed + 9u, 1.2, 2.4, [&](double age, double life, Rng& r, int) {
            const double u = age / life, y = TOP + 10 + r.range(0, 90) + u * r.range(6, 14), q = r.range(-1, 1),
                         x = q * halfW(y) + std::sin(age * 1.7 + q * 5) * 1.5, k = std::sin(u * PI) * ray(q, t);
            if (k > .12) p.dot(x, y, k > .45 ? WHITE : C[6], std::min(1., k * 2.2));
        });
    }
};
}  // namespace
const Descriptor& blessingDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{{PixelFxKind::Blessing, "holy/blessing", 80, 128, 40, 108, T, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed         = 7;
                                  s.actor.source = PixelFxActorSource::Dummy;
                                  s.palette      = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                                                    hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
                                         },
                                         nullptr,
                                         options,
                                         nullptr,
                                         nullptr,
                                         [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<Blessing>(s);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
