// eve-hot-path
// Native adaptation of pixelfx scifi/tractor-beam.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3, LENS = -84, TOPW = 5.5, BOTW = 32, POP = .1, HOVER = .32, RISE = 1.4, LIFE = POP + HOVER + RISE;
double           half(double y) { return lerp(BOTW, TOPW, clamp(y / LENS)); }
struct Haul {
    double x, y, z;
};
Haul haul(double r0, double th0, double dir, double turns, double k) {
    const double s = std::pow(clamp(k), 1.8), y = lerp(-2, LENS + 5, s), th = th0 + dir * TAU * turns * s,
                 rr = r0 * (half(y) / BOTW) * (1 - .5 * s);
    return {std::cos(th) * rr, y, std::sin(th)};
}
struct Rock {
    uint32_t id;
    double   born, r0, th0, dir, size, a0, va;
};
struct RockState {
    double x, y, z, px, py, size, a, lift;
};
RockState rockAt(const Rock& r, double age) {
    const double lift = clamp((age - POP - HOVER) / RISE);
    if (lift > 0) {
        const auto q = haul(r.r0, r.th0, r.dir, 1.3, lift), prev = haul(r.r0, r.th0, r.dir, 1.3, lift - .07);
        return {
            q.x, q.y, q.z, prev.x, prev.y, r.size * (1 - .6 * std::pow(lift, 2.5)), r.a0 + r.va * age * (.3 + lift * 2),
            lift};
    }
    const double y   = age < POP ? lerp(3, -2, age / POP) : -2 - 2 * smooth(0, HOVER, age - POP),
                 wob = age > POP ? std::sin(age * 55 + r.th0 * 9) * .7 : 0;
    return {std::cos(r.th0) * r.r0 + wob, y, 1, 0, 0, r.size, r.a0 + r.va * age * .3, 0};
}
struct TractorBeam final : AnalyticKernel {
    std::array<Rock, 7> rocks{};
    scifi::Ghosts       ghosts{D, 0, std::array<double, 4>{-58, -112, 58, 14}};
    explicit TractorBeam(const PixelFxSettings& o) {
        const auto& C = o.palette;
        const auto& S = o.scifi.stone;
        Rng         rn(o.seed);
        for (size_t i = 0; i < rocks.size(); ++i) {
            const double born = (double(i) / 7) * D + rn.range(-.06, .06), r0 = rn.range(8, 25),
                         th0 = (i % 2 ? 0 : PI) + rn.range(-.45, .45), dir = rn.sign(), size = rn.range(2.4, 4.4),
                         a0 = rn.range(0, TAU), va = rn.range(-7, 7);
            rocks[i] = {uint32_t(i) + o.seed * 31u, born, r0, th0, dir, size, a0, va};
        }
        for (const auto& r : rocks) {
            const double x = std::cos(r.th0) * r.r0;
            for (int i = 0; i < 5; ++i) {
                const double px = x + rn.range(-3, 3), vx = rn.range(-34, 34), vy = -rn.range(8, 26),
                             life = rn.range(.5, .9), size = rn.range(2.2, 3.6);
                ghosts.add(r.born + .02, ParticleKind::Dust, px, -1, vx, vy, life, mix(S[3], C[4], .3), size);
            }
            for (int i = 0; i < 6; ++i) {
                const double                  px = x + rn.range(-2, 2), vx = rn.range(-45, 45), vy = -rn.range(60, 150),
                                              life = rn.range(.5, .9);
                const std::array<uint32_t, 3> colors{S[2], S[3], S[4]};
                const auto                    color = rn.pick<uint32_t>(colors);
                const double                  size  = rn.isChance(.3) ? 2 : 1;
                ghosts.add(r.born + .02, ParticleKind::Grit, px, -2, vx, vy, life, color, size);
            }
        }
        for (int i = 0; i < 16; ++i) {
            const int    sign = rn.sign();
            const double born = rn.range(0, D), x = sign * rn.range(14, 34), y = -rn.range(0, 3),
                         vx = -sign * rn.range(6, 22), vy = -rn.range(4, 14), life = rn.range(.9, 1.5),
                         size = rn.range(2.6, 4.4);
            ghosts.add(born, ParticleKind::Dust, x, y, vx, vy, life, mix(S[3], C[5], .3), size);
        }
    }
    static void rock(Paint& p, const RockState& q, const Rock& r, std::span<const uint32_t> S,
                     std::span<const uint32_t> C, bool back) {
        const double   R = std::max(.9, q.size), ca = std::cos(q.a), sa = std::sin(q.a);
        const uint32_t lit = mix(S[4], C[6], .3 + .6 * q.lift);
        const auto     rad = [&](double ang) {
            const double f = wrap((ang / TAU) * 6, 6), i = std::floor(f), k = f - i;
            return R * (.75 + .35 * lerp(rand({r.id, uint32_t(i)}), rand({r.id, uint32_t(std::fmod(i + 1, 6))}), k));
        };
        if (q.lift > .15 && !back) {
            p.lineFn(
                q.px, q.py, q.x, q.y, [&](double u) { return u > .6 ? C[5] : C[3]; }, [](double u) { return .7 * u; });
            for (int i = 1; i <= 3; ++i)
                p.disc(lerp(q.x, q.px, double(i) / 3), lerp(q.y, q.py, double(i) / 3) + 1, R * (.5 + .25 * i),
                       mix(S[3], C[5], .4), .22 * (1 - double(i) / 4) * q.lift);
        }
        p.shade(q.x - R - 2, q.y - R - 2, q.x + R + 2, q.y + R + 2, [&](double x, double y, int sx, int sy) {
            const double dx = x - q.x, dy = y - q.y, u = dx * ca + dy * sa, v = -dx * sa + dy * ca, d = jsHypot2(u, v),
                         e = rad(std::atan2(v, u));
            if (d > e + .8) return;
            p.put(sx, sy,
                  d > e           ? S[0]
                  : back          ? (dy < -.3 * R ? S[3] : S[1])
                  : dy < -.45 * R ? (q.lift > .35 && d > e - 1 ? C[6] : lit)
                  : dy < -.1 * R  ? (dx < 0 ? S[4] : S[3])
                  : dy > .35 * R  ? S[1]
                                  : S[2]);
        });
        if (!back && q.lift > 0) p.glow(q.x, q.y - R, 5 + R, C[5], .25 * q.lift);
    }
    static void pod(Paint& p, double t, std::span<const uint32_t> C, std::span<const uint32_t> H, double y0) {
        const double y = jsRound(y0);
        p.shade(-21, y - 12, 21, y + 6, [&](double x, double yy, int sx, int sy) {
            const double dy = yy - y, cx = x / 8.5, cy = (dy + 5) / 6;
            if (cy < 0 && cx * cx + cy * cy <= 1) {
                const double d = cx * cx + cy * cy, l = -cx * .6 - cy * .8;
                p.put(sx, sy, d > .8 ? H[0] : l > .85 ? WHITE : l > .6 ? C[6] : l > .2 ? C[4] : C[3]);
                return;
            }
            const double hx = x / 20, hy = (dy + .5) / 4.5;
            if (dy <= 0 && hx * hx + hy * hy <= 1) {
                const double d = hx * hx + hy * hy, l = -hx * .55 - hy * .7;
                p.put(sx, sy,
                      d > .88    ? H[0]
                      : dy == -2 ? H[1]
                      : l > .55  ? H[5]
                      : l > .25  ? H[4]
                      : l > -.1  ? H[3]
                                 : H[2]);
                return;
            }
            const double w = 19.5 - dy * 2.4;
            if (dy > 0 && dy <= 5 && std::abs(x) <= w)
                p.put(sx, sy, std::abs(x) > w - 1 ? H[0] : dy == 1 ? H[2] : dy >= 4 && std::abs(x) < 9 ? C[3] : H[1]);
        });
        for (int i = 0; i < 6; ++i) {
            const double lx = -15 + i * 6, k = .5 + .5 * std::cos(TAU * (4 * (t / D) - double(i) / 6));
            p.dot(lx, y + 1, k > .7 ? WHITE : k > .35 ? C[5] : C[2]);
            if (k > .5) p.glow(lx, y + 1, 5, C[5], .35 * k);
        }
        p.ellipse(0, y + 5.5, 7.5, 2.2, [&](double d) {
            return d < .45 ? WHITE : d < .75 ? C[6] : d < .9 ? C[5] : C[3];
        });
    }
    static void hoop(Paint& p, double y, double rx, uint32_t c, bool back) {
        const double ry = rx * .17;
        const int    n  = int(std::ceil(rx * 7));
        for (int i = 0; i < n; ++i) {
            const double g = (double(i) / n) * TAU, s = std::sin(g);
            if ((s >= 0) == back) continue;
            p.dot(std::cos(g) * rx, y + s * ry, c);
        }
    }
    void drawBack(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto& C = o.palette;
        const auto& S = o.scifi.stone;
        for (int y = -6; y <= 6; ++y)
            for (int x = -int(BOTW); x <= BOTW; ++x) {
                const double d = jsHypot2((x + .5) / BOTW, (y + .5) / 5.5);
                if (d <= 1) p.blend(x + .5, y + .5, d < .35 ? C[6] : C[5], .2 + .55 * std::pow(1 - d, 1.5));
            }
        hoop(p, 0, BOTW + 1, C[5], true);
        ghosts.evaluate(t).draw(p, Paint::Layer::Back);
        for (const auto& r : rocks) {
            const double age = scifi::since(t, r.born, D);
            if (age >= LIFE) continue;
            const auto q = rockAt(r, age);
            if (q.z < 0 && q.lift > 0) rock(p, q, r, S, C, true);
        }
    }
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&  C  = o.palette;
        const auto&  S  = o.scifi.stone;
        const auto&  H  = o.scifi.hull;
        const double ph = t / D, podY = LENS - 5 + 1.5 * std::sin(TAU * 2 * ph);
        for (int y = int(std::ceil(LENS + 3)); y < 0; ++y) {
            const double w = half(y), v = (y - LENS) / -LENS, pulse = wrap(v * 3.2 + ph * 6, 1) < .08 ? 1 : 0;
            for (int x = int(std::floor(-w)) - 1; x <= std::ceil(w); ++x) {
                const double q = std::abs(x + .5) / w;
                if (q > 1 + 1.2 / w) continue;
                if (q > 1)
                    p.dot(x, y, C[4], .6 - .3 * v);
                else if (q > 1 - 1 / w)
                    p.dot(x, y, v < .2 ? WHITE : v < .45 ? C[6] : C[5]);
                else {
                    double curtain = 0;
                    for (uint32_t s = 0; s < 4; ++s)
                        if (std::abs((x + .5) / w - (rand({o.seed, s, 7}) - .5) * 1.4 -
                                     .12 * std::sin(TAU * ph + s * 1.7)) < .045)
                            curtain = 1;
                    const double core = std::pow(1 - q, 2) * std::pow(1 - v, 1.6),
                                 a    = .08 + .22 * std::pow(q, 4) + .6 * core + .12 * pulse + .18 * curtain;
                    p.blend(x + .5, y + .5,
                            core > .45                              ? WHITE
                            : curtain != 0 || pulse != 0 || v < .35 ? C[5]
                                                                    : C[4],
                            clamp(a));
                }
            }
            if ((y & 3) == 0)
                for (double s : {-1., 1.}) p.glow(s * w, y, 8, C[4], .18);
        }
        for (int i = 0; i < 5; ++i) {
            const double k = scifi::since(t, (double(i) / 5) * D, D) / 1.1;
            if (k >= 1) continue;
            const double y = lerp(0, LENS + 5, ease::inQuad(k)), w = half(y) + 1;
            hoop(p, y, w, C[3], true);
            hoop(p, y, w, k < .12 ? WHITE : C[6], false);
            p.glow(0, y + w * .17, w + 4, C[5], .12);
        }
        cycle(t, D, 22, o.seed + 1, .6, 1, [&](double age, double life, Rng& r, int) {
            const double k = age / life, y = lerp(-2, LENS + 5, k * k), w = half(y), x = r.range(-.85, .85) * w,
                         L = 2 + 6 * k;
            p.lineFn(x, y, x, y + L, [&](double u) { return u < .3 ? WHITE : C[5]; }, std::min(1., (1 - k) * 3));
        });
        cycle(t, D, 9, o.seed + 3, 1.4, 2, [&](double age, double life, Rng& r, int) {
            const double k = age / life, r0 = r.range(8, 26), th0 = r.range(0, TAU), dir = r.sign();
            const auto   q = haul(r0, th0, dir, .8, k);
            p.disc(q.x, q.y, lerp(4, 1.5, k), mix(S[3], C[5], .25 + .5 * k), .4 * std::min(1., k * 6) * (1 - k));
        });
        cycle(t, D, 46, o.seed + 2, .8, 1.3, [&](double age, double life, Rng& r, int) {
            const double k = age / life, r0 = r.range(6, 30), th0 = r.range(0, TAU), dir = r.sign(),
                         turns = r.range(1, 1.8);
            const bool   mote  = r.isChance(.45);
            const auto   q = haul(r0, th0, dir, turns, k), prev = haul(r0, th0, dir, turns, k - (mote ? .13 : .06));
            const std::array<uint32_t, 3> colors{S[3], S[4], S[4]};
            const uint32_t                c = mote ? (r.isChance(.5) ? WHITE : C[6]) : r.pick<uint32_t>(colors);
            p.lineFn(
                prev.x, prev.y, q.x, q.y,
                [&](double u) { return u > .5 ? (q.z > 0 ? c : scale(c, .7))
                                       : mote ? C[4]
                                              : S[2]; }, 1.);
            if (mote) {
                p.dot(q.x, q.y, WHITE);
                p.glow(q.x, q.y, 4, C[5], .45);
            }
        });
        cycle(t, D, 8, o.seed + 4, .5, [&](double age, double life, Rng& r, int) {
            const double k = age / life, r0 = r.range(4, 24), th0 = r.range(0, TAU), dir = r.sign(),
                         at  = r.range(.1, .7) + k * .15;
            const auto   q   = haul(r0, th0, dir, .6, at);
            const double arm = jsRound(2 * std::sin(PI * k));
            if (arm > 0) p.sparkle(q.x, q.y, arm, C[6], C[5], .4);
        });
        for (const auto& r : rocks) {
            const double age = scifi::since(t, r.born, D);
            if (age >= LIFE) continue;
            const auto q = rockAt(r, age);
            if (q.z >= 0 || q.lift <= 0) rock(p, q, r, S, C, false);
            if (age > LIFE - .1) p.sparkle(q.x, LENS + 3, 5, C[6], C[5], .5);
        }
        for (const auto& r : rocks) {
            const double a = t - (r.born + LIFE - .08);
            if (a < 0 || a >= .28) continue;
            const double q = 1 - a / .28;
            p.glow(0, LENS + 3, 30 * (.55 + .45 * q), C[5], .7 * q * q);
        }
        ghosts.evaluate(t).draw(p, Paint::Layer::Front);
        hoop(p, 0, BOTW + 1, WHITE, false);
        hoop(p, 1, BOTW, C[5], false);
        pod(p, t, C, H, podY);
        const double fl = .85 + .15 * std::sin(TAU * 6 * ph);
        p.glow(0, LENS + 6, 36, C[5], .7 * fl);
        p.glow(0, LENS + 6, 12, WHITE, .7 * fl);
        for (int y = int(LENS) + 16; y < -8; y += 14) p.glow(0, y, half(y) + 10, C[4], .16);
        for (int x : {-20, 0, 20}) p.glow(x, -2, 22, C[4], .26);
    }
};
}  // namespace
const Descriptor& tractorBeamDescriptor() {
    static const std::array<Option, 2> options{{{"hull", 1, 64, false, 6, &PixelFxScifiSettings::hull},
                                                {"stone", 1, 64, false, 5, &PixelFxScifiSettings::stone}}};
    static const Descriptor            d{
        {PixelFxKind::TractorBeam, "scifi/tractor-beam", 120, 132, 60, 114, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 9;
            s.palette     = {hex(0x06161a), hex(0x0b2e33), hex(0x11505a), hex(0x1a7f84),
                             hex(0x2bb5a8), hex(0x7ef0c8), hex(0xd6fff0), WHITE};
            s.scifi.hull  = {hex(0x0d0f17), hex(0x1c2030), hex(0x2e3448), hex(0x4a5470), hex(0x7a86a8), hex(0xb8c4e0)};
            s.scifi.stone = {hex(0x1a1622), hex(0x2e2838), hex(0x4a4258), hex(0x6d6480), hex(0x9a90aa)};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
            return std::make_unique<TractorBeam>(s);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
