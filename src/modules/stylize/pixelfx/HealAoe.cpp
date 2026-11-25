// eve-hot-path
// Native adaptation of pixelfx holy/heal-aoe.js and holy/_lib.js plus, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SY = .2, SEED = .3, ROLL = 1;
void             plus(Paint& p, double x, double y, int L, int th, std::span<const uint32_t> C, double a, bool glint) {
    const double X = jsRound(x), Y = jsRound(y);
    const int    w   = th > 1 ? 1 : 0;
    const size_t n   = C.size();
    const auto   ins = [&](int i, int j) {
        return (std::abs(i) <= w && std::abs(j) <= L) || (std::abs(j) <= w && std::abs(i) <= L);
    };
    for (int j = -L - 1; j <= L + 1; ++j)
        for (int i = -L - 1; i <= L + 1; ++i) {
            if (!ins(i, j)) {
                if (w && (ins(i - 1, j) || ins(i + 1, j) || ins(i, j - 1) || ins(i, j + 1)))
                    p.dot(X + i, Y + j, C[n - 6], a * .9);
                continue;
            }
            uint32_t c;
            if (!w)
                c = i == 0 && j == 0 ? WHITE : std::abs(i) + std::abs(j) >= L ? C[n - 3] : C[n - 2];
            else if (i == 0 && j == 0)
                c = WHITE;
            else if (i == 0 || j == 0)
                c = std::abs(i) + std::abs(j) < L ? C[n - 2] : C[n - 3];
            else
                c = i < 0 || j < 0 ? C[n - 3] : C[n - 5];
            if (glint && ((i == 0 && j == -L) || (i == -L && j == 0))) c = WHITE;
            p.dot(X + i, Y + j, c, a);
        }
}
struct Site {
    double x, z, start, rise, life, sway;
    int    length, thickness;
    bool   popped = false;
};
struct Rider {
    double angle, omega;
};
struct HealAoe final : Kernel {
    const std::vector<uint32_t> C;
    const double                reach;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::array<Site, 26>        sites;
    std::array<Rider, 14>       riders;
    double                      t        = 0;
    bool                        released = false;
    explicit HealAoe(const PixelFxSettings& o) : C(o.palette), reach(o.holy.reach), rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        for (size_t i = 0; i < sites.size(); ++i) {
            const double a = rng.range(0, TAU), d = reach * std::sqrt(rng.range(.04, .92));
            const bool   big   = i % 5 < 2;
            const double start = passed(d) + rng.range(0, .06), rise = rng.range(18, 34), life = rng.range(.9, 1.3),
                         sway = rng.range(0, TAU);
            sites[i]          = {std::cos(a) * d,
                                 std::sin(a) * d,
                                 start,
                                 rise,
                                 life,
                                 sway,
                                 big ? 3 + int(i % 5 == 0) : i % 5 == 2 ? 1 : 2,
                                 big ? 3 : 1};
        }
        for (size_t i = 0; i < riders.size(); ++i) {
            const double a = (double(i) / 14) * TAU + rng.range(-.2, .2), w = rng.range(.6, 1.1) * (i % 2 ? 1 : -1);
            riders[i] = {a, w};
        }
    }
    double front() const { return t < SEED ? 0 : reach * ease::outCubic(seg(t, SEED, SEED + ROLL)); }
    double passed(double d) const { return SEED + ROLL * (1 - std::cbrt(1 - std::min(.999, d / reach))); }
    void   wash(Paint& p) const {
        const double fr = front();
        if (fr < 2 || t > 2.6) return;
        p.shade(-fr - 1, -fr * SY - 1, fr + 1, fr * SY + 1, [&](double x, double y, int sx, int sy) {
            const double d = jsHypot2(x, y / SY);
            if (d > fr) return;
            const double age = t - passed(d),
                         k   = std::exp(-age / .35) * smooth(fr, fr - 6, d) * (1 - smooth(1.4, 1.9, t));
            if (k < .04) return;
            p.add(sx, sy, C[4], .32 * k);
        });
    }
    void ring(Paint& p, bool back) const {
        if (t < SEED) return;
        const double u = seg(t, SEED, SEED + ROLL + .25), fr = front(), k = u < .7 ? 1 : (1 - u) / .3;
        if (k <= 0) return;
        const auto keep = [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; };
        band(p, 0, 0, fr, SY, std::max(2., 7 * (1 - u * .7)), k, std::span(C).subspan(2), keep, t < SEED + .08);
        holy::oval(
            p, 0, 0, fr + 1, (fr + 1) * SY, [&](double g) { return std::sin(g) > .3 ? WHITE : C[6]; },
            k * (back ? .7 : 1), keep);
        const double n = std::ceil(fr * .9);
        for (int j = 0; j < n; ++j) {
            const double g    = (double(j) / n) * TAU;
            const bool   near = std::sin(g) >= 0;
            if (near == back || rand({uint32_t(j), jsUint(std::floor(t * 20))}) > .45) continue;
            const double x = std::cos(g) * fr, y = std::sin(g) * fr * SY,
                         h = (1 + rand({uint32_t(j), 7}) * 4) * k * (near ? 1 : .6);
            for (int i = 1; i <= h; ++i) p.dot(x, y - i, i > h - 1 ? WHITE : C[6], k * (1.2 - i / (h + 1)));
        }
        for (const auto& r : riders) {
            const double g    = r.angle + r.omega * (t - SEED) * 2.2;
            const bool   near = std::sin(g) >= 0;
            if (near == back) continue;
            const double x = std::cos(g) * fr, y = std::sin(g) * fr * SY;
            if (near)
                p.sparkle(x, y - 1, 2 + (std::sin(t * 9 + r.angle * 5) > .3 ? 1 : 0), WHITE, C[6], .35 * k);
            else
                p.dot(x, y, C[6], k);
        }
        if (!back)
            for (int i = 0; i < 8; ++i) {
                const double g = (double(i) / 8) * TAU;
                p.glow(std::cos(g) * fr, std::sin(g) * fr * SY - 2, 16, C[4], .22 * k);
            }
    }
    void sprouts(Paint& p, bool back) const {
        for (const auto& s : sites) {
            const double a = t - s.start;
            if (a < 0 || a > s.life || (s.z < 0) != back) continue;
            const double u = a / s.life, h = s.rise * ease::outCubic(u), x = s.x + std::sin(a * 4 + s.sway) * 1.5 * u,
                         y   = s.z * SY - 2 - h;
            const double pop = ease::outBack(clamp(a / .12), 2.6), end = s.life - .15;
            const int    L = int(std::max(1., jsRound(s.length * pop - (back ? 1 : 0))));
            if (a < .04)
                p.sparkle(x, y, 3, WHITE, C[6], .5);
            else if (a < end)
                plus(p, x, y, L, back && L < 3 ? 1 : s.thickness, back ? std::span(C).first(7) : std::span(C),
                     back ? .8 : 1, !back && std::sin(a * 12 + s.sway * 3) > .8);
            else {
                const double v = (a - end) / .15;
                for (auto d : std::array<Point, 4>{{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}})
                    p.dot(x + d.x * (2 + v * 5), y + d.y * (2 + v * 5) - v * 2, v < .4 ? WHITE : C[6], 1 - v);
            }
            if (u < .5)
                for (double j = 1; j < h * (1 - u * 2); j += 1.5)
                    p.dot(s.x, s.z * SY - 2 - h + j + 3, C[5], (1 - j / h) * (1 - u * 2) * (back ? .5 : .8));
            p.glow(x, y, 7 + L * 2, C[5], (back ? .2 : .34) * (a < end ? 1 : 1 - (a - end) / .15));
        }
    }
    void update(double dt) override {
        t += dt;
        if (t < SEED - .04 && rng.isChance(.7)) {
            const double a = rng.range(0, TAU), d = rng.range(24, 48), x = std::cos(a) * d,
                         y = std::sin(a) * d * SY - rng.range(1, 8);
            const double T = rng.range(.16, .24), l = jsHypot2(x, y + 3), v = (l * 3) / (std::exp(3 * T) - 1);
            const auto   c = rng.isChance(.5) ? C[6] : C[5];
            ps.spawn(rng, ParticleKind::Streak, x, y, (-x / l) * v, ((-3 - y) / l) * v, T, c);
        }
        if (!released && t >= SEED) {
            released = true;
            lights.add(0, -16, 76, C[5], .85, .55);
            lights.add(0, -6, 28, WHITE, .8, .12);
            for (int i = 0; i < 10; ++i) {
                const double x = rng.range(-2, 2), y = -rng.range(2, 10), vx = rng.range(-25, 25),
                             vy = -rng.range(80, 170), life = rng.range(.3, .6);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 1);
                ps.spawn(rng, ParticleKind::Mote, x, y, vx, vy, life, c, size);
            }
            for (int i = 0; i < 16; ++i) {
                const double a = rng.range(PI * 1.05, PI * 1.95), sp = rng.range(40, 120), life = rng.range(.4, .8);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 2);
                ps.spawn(rng, ParticleKind::Star, 0, -3, std::cos(a) * sp, std::sin(a) * sp, life, c, size);
            }
        }
        for (auto& s : sites)
            if (!s.popped && t >= s.start) {
                s.popped = true;
                for (int i = 0; i < 3; ++i) {
                    const double x = s.x + rng.range(-3, 3), vx = rng.range(-10, 10), vy = -rng.range(15, 40),
                                 life = rng.range(.3, .6);
                    const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                    const double size = rng.range(.5, 1.2);
                    ps.spawn(rng, ParticleKind::Star, x, s.z * SY - 1, vx, vy, life, c, size, 0,
                             s.z < 0 ? ParticleFlag::Back : ParticleFlag::None);
                }
            }
        if (t > SEED + .1 && t < 1.9 && rng.isChance(.35)) {
            const double fr = front(), a = rng.range(0, TAU), d = fr * std::sqrt(rng.next()), vx = rng.range(-5, 5),
                         vy = -rng.range(10, 30), life = rng.range(.5, 1);
            ps.spawn(rng, ParticleKind::Ember, std::cos(a) * d, std::sin(a) * d * SY - 1, vx, vy, life, C[5], 1, 0,
                     std::sin(a) < 0 ? ParticleFlag::Back : ParticleFlag::None);
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        wash(p);
        ring(p, true);
        sprouts(p, true);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        ring(p, false);
        if (t < SEED) {
            const double k = t / SEED;
            p.sparkle(0, -3, 2 + 4 * k * k, WHITE, C[6], .3 + .4 * k);
            p.glow(0, -3, 10 + 16 * k, C[4], .5 * k);
        } else if (t < SEED + .2)
            flare(p, 0, -3, 22 * std::pow(1 - (t - SEED) / .2, 1.5), 1 - (t - SEED) / .2, std::span(C).subspan(3));
        const double g = seg(t, SEED, SEED + .5);
        if (g > 0 && g < 1) {
            const double top = -4 - 46 * ease::outCubic(std::min(1., g * 2.2)) - 10 * g,
                         bot = -2 - 50 * ease::inQuad(seg(g, .35, 1)), w = 1 + 2.4 * (1 - g);
            if (bot - top > 2) {
                p.beam(0, bot, 0, top, w * 2, [&](double u, double v) {
                    return v < .45 ? WHITE : u > .8 ? C[5] : C[6];
                });
                p.glowLine(0, bot, 0, top, 12, C[5], .55 * (1 - g));
                p.sparkle(0, top, 4 * (1 - g) + 1, WHITE, C[6], .5 * (1 - g));
            }
        }
        sprouts(p, false);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& healAoeDescriptor() {
    static const std::array<Option, 1> options{{{"reach", 1, 256, false, 0, &PixelFxHolySettings::reach}}};
    static const Descriptor            d{
        {PixelFxKind::HealAoe, "holy/heal-aoe", 256, 136, 128, 104, 2.9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed       = 6;
            s.holy.reach = 104;
            s.palette    = {hex(0x0e2a24), hex(0x14472f), hex(0x1d6e3a), hex(0x2f9a45),
                            hex(0x5cc654), hex(0xa0e46c), hex(0xdcffb4), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HealAoe>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
