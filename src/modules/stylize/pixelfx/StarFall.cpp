// eve-hot-path
// Native adaptation of pixelfx arcane/star-fall.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr int    N      = 11;
constexpr double SPREAD = 46, H = 124, SLANT = .38, TRAIL = .17;
struct Star {
    double x, y, sx, sy, t0, duration;
    bool   landed, big;
    double phase;
};
struct Hit {
    double x, y, time;
    bool   big;
};
struct Position {
    double x, y, q;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct StarFall final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::array<Star, N>         stars{};
    std::array<Hit, N>          hits{};
    size_t                      hitCount = 0;
    double                      t        = 0;
    explicit StarFall(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        std::array<double, N> xs{};
        for (int i = 0; i < N; ++i) xs[size_t(i)] = -SPREAD + ((i + R.range(.2, .8)) * 2 * SPREAD) / N;
        for (int i = N - 1; i > 0; --i) {
            const int j = R.integer(0, i);
            std::swap(xs[size_t(i)], xs[size_t(j)]);
        }
        for (int i = 0; i < N; ++i) {
            const double x = xs[size_t(i)], y = R.range(-3, 2), h = H * R.range(.85, 1);
            const double t0 = .04 + i * .105 + R.range(0, .05), duration = R.range(.3, .4);
            const bool   big   = R.isChance(.35);
            const double phase = R.range(0, TAU);
            stars[size_t(i)]   = {x, y, x - h * SLANT, y - h, t0, duration, false, big, phase};
        }
    }
    void            emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    static Position at(const Star& s, double tt) {
        const double q = clamp((tt - s.t0) / s.duration), e = q * (.35 + .65 * q);
        return {s.sx + (s.x - s.sx) * e, s.sy + (s.y - s.sy) * e, q};
    }
    void star(Paint& p, const Star& s) const {
        const auto [x, y, q] = at(s, t);
        (void)q;
        constexpr int n = 20;
        for (int k = n; k >= 1; --k) {
            const auto   a = at(s, t - (k * TRAIL) / n), b = at(s, t - ((k - 1) * TRAIL) / n);
            const double u = double(k) / n;
            if (t - (k * TRAIL) / n < s.t0) continue;
            const auto c = u < .15 ? WHITE : u < .4 ? C[6] : u < .7 ? C[5] : C[4];
            if (u < (s.big ? .55 : .35)) p.line(a.x + 1, a.y, b.x + 1, b.y, u < .2 ? C[6] : C[5], 1.1 - u * 1.4);
            p.line(a.x, a.y, b.x, b.y, c, 1.3 - u);
            if (k % 3 == 0) p.glow(a.x, a.y, 7 * (1 - u) + 2, C[5], .3 * (1 - u));
        }
        if (s.big)
            flare(p, x, y, 7 + 1.5 * std::sin(t * 40 + s.phase), .8, C);
        else
            p.sparkle(x, y, 3.5 + std::sin(t * 40 + s.phase), C[6], C[5], .6);
        p.circle(x, y, s.big ? 1.6 : 1.1, WHITE);
        p.glow(x, y, s.big ? 18 : 13, C[5], .6);
        p.glow(x, y, 5, WHITE, .35);
    }
    void hit(Paint& p, const Hit& h, bool far) const {
        const double k = (t - h.time) / .45;
        if (k < 0 || k >= 1) return;
        const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
        band(
            p, h.x, h.y, 3 + (h.big ? 22 : 16) * ease::outCubic(k), .26, std::max(1., 2.5 * (1 - k)), 1.15 - k, ramp,
            [&](double g) { return (std::sin(g) < 0) == far; }, k < .2);
    }
    void update(double dt) override {
        t += dt;
        for (auto& s : stars) {
            if (t < s.t0 || s.landed) continue;
            const auto [x, y, q] = at(s, t);
            if (R.isChance(.55)) {
                // Explicit locals retain source argument evaluation/RNG consumption order.
                const double px = x + R.range(-1.5, 1.5), py = y + R.range(-1.5, 1.5), vx = R.range(-12, 12),
                             vy = R.range(-8, 4), life = R.range(.35, .7);
                const std::array<uint32_t, 3> colors{WHITE, C[6], C[5]};
                const auto                    color = R.pick<uint32_t>(colors);
                const double                  size  = R.isChance(.7) ? R.range(.4, .9) : R.range(1.2, 1.8);
                emit({ParticleKind::Star, px, py, vx, vy, life, color, size});
            }
            if (q >= 1) {
                s.landed = true;
                EV_ASSERT(hitCount < hits.size(), "Each star may land only once");
                hits[hitCount++] = {s.x, s.y, t, s.big};
                lights.add(s.x, s.y - 12, s.big ? 46 : 38, C[5], .9, .32);
                lights.add(s.x, s.y - 3, 16, WHITE, .8, .08);
                for (int i = 0; i < (s.big ? 14 : 9); ++i) {
                    const double a = -PI / 2 + R.range(-1.35, 1.35), sp = R.range(50, 150);
                    emit({ParticleKind::Spark, s.x, std::min(-1., s.y - 1), std::cos(a) * sp, std::sin(a) * sp,
                          R.range(.2, .5)});
                }
                const std::array<uint32_t, 2> colors{WHITE, C[6]};
                for (int i = 0; i < 4; ++i) {
                    const double px = s.x + R.range(-4, 4), py = s.y - R.range(2, 8), vx = R.range(-15, 15),
                                 vy = -R.range(10, 35), life = R.range(.4, .8);
                    const auto   color = R.pick<uint32_t>(colors);
                    const double size  = R.range(.8, 2);
                    emit({ParticleKind::Star, px, py, vx, vy, life, color, size});
                }
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = clamp(t / .3) * (1 - clamp((t - 1.7) / .7));
        if (k > 0)
            for (double x : {-36., 0., 36.}) p.glow(x, -1, 40, C[4], .14 * k);
        for (size_t i = 0; i < hitCount; ++i) hit(p, hits[i], true);
        for (size_t i = 0; i < hitCount; ++i) {
            const auto&  h  = hits[i];
            const double k2 = 1 - (t - h.time) / .7;
            if (k2 <= 0) continue;
            p.dot(h.x, h.y, k2 > .6 ? WHITE : C[6], k2 * 1.5);
            for (double dx : {-1., 1.}) p.dot(h.x + dx, h.y, C[5], k2 * 1.2);
            p.glow(h.x, h.y - 1, 10, C[5], .35 * k2);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        for (const auto& s : stars)
            if (t >= s.t0 && !s.landed) star(p, s);
        for (size_t i = 0; i < hitCount; ++i) {
            const auto& h = hits[i];
            hit(p, h, false);
            const double k = (t - h.time) / .22;
            if (k < 0 || k >= 1) continue;
            if (k < .35) p.circle(h.x, h.y - 2, 5 * (1 - k / .35) + 1, WHITE);
            flare(p, h.x, h.y - 2, (h.big ? 16 : 12) * std::pow(1 - k, 1.5) + 2, 1 - k, C);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& starFallDescriptor() {
    static const Descriptor d{
        {PixelFxKind::StarFall, "arcane/star-fall", 232, 172, 124, 150, 2.5, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 13;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<StarFall>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
