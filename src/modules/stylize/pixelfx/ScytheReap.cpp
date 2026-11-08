// eve-hot-path
// Native adaptation of pixelfx melee/scythe-reap.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double WIND = .08, SWEEP = .34, T1 = WIND + SWEEP, T2 = T1 + .14, T3 = T2 + .55;
double           head(double t) { return ease::inOutSine(clamp((t - WIND) / SWEEP)); }
struct Wisp {
    double             x, y, vx, vy, life, age, seed;
    std::vector<Point> points;
    bool               far;
};
struct ScytheReap final : Kernel {
    const std::vector<uint32_t>   C;
    const std::array<uint32_t, 8> dim;
    const melee::Swing            A{8, -33, 52, .6, .08, -.5, 1, .1, 3.5};
    Rng                           R;
    Particles                     ps;
    Lights                        lights;
    std::vector<Wisp>             wisps;
    double                        t = 0, lastTear = 0;
    explicit ScytheReap(const PixelFxSettings& o)
        : C(o.palette), dim{C[0], C[0], C[1], C[2], C[3], C[4], C[5], C[6]}, R(o.seed) {
        ps.haze = C[5];
    }
    double tear() const { return smooth(T2, T3, t); }
    void   addWisp(double x, double y, bool far) {
        const double vx = R.range(-12, 12), vy = -R.range(8, 26), life = R.range(.5, .9), seed = R.range(0, 100);
        Wisp         w{x, y, vx, vy, life, 0, seed, {}, far};
        w.points.reserve(15);
        wisps.push_back(std::move(w));
    }
    void update(double dt) override {
        t += dt;
        if (t < WIND && R.isChance(.5)) {
            const auto   v = melee::onSwing(A, A.R - 4, 0);
            const double x = v.x + R.range(-14, 14), y = v.y + R.range(-8, 8), vx = R.range(-10, 10),
                         vy = R.range(-6, 6), life = R.range(.3, .5), size = R.range(1.5, 2.5);
            ps.spawn(R, ParticleKind::Vapour, x, y, vx, vy, life, C[5], size);
        }
        if (t > WIND && t < T1 && R.isChance(.5)) {
            const double u  = A.span * head(t) * R.range(.75, 1);
            const auto   tr = melee::travel(A, u);
            const double vy = -R.range(4, 14), life = R.range(.5, .9), size = R.range(2, 3.5);
            ps.spawn(R, ParticleKind::Vapour, tr.x, tr.y, tr.dx * 20, vy, life, C[6], size, 0,
                     std::sin(tr.a) < 0 ? ParticleFlag::Back : ParticleFlag::None);
        }
        const double k = tear();
        for (double u = lastTear; u < k * 1.15; u += .022) {
            const double uu       = std::min(1., u) * A.span;
            auto         geometry = A;
            geometry.R            = A.R - R.range(0, 8);
            const auto tr         = melee::travel(geometry, uu);
            const bool far        = std::sin(tr.a) < 0;
            addWisp(tr.x, tr.y, far);
            if (R.isChance(.5)) {
                const double vx = R.range(-10, 10), vy = -R.range(6, 18), life = R.range(.55, .85);
                const auto   color = R.isChance(.5) ? C[5] : C[4];
                const double size  = R.range(2.5, 4.5);
                ps.spawn(R, ParticleKind::Vapour, tr.x, tr.y, vx, vy, life, color, size, 0,
                         far ? ParticleFlag::Back : ParticleFlag::None);
            }
            if (R.isChance(.2)) {
                const double x = tr.x + R.range(-3, 3), y = tr.y + R.range(-3, 3), life = R.range(.25, .5);
                const auto   color = R.isChance(.5) ? C[6] : WHITE;
                const double size  = R.range(.6, 1.6);
                ps.spawn(R, ParticleKind::Star, x, y, 0, -6, life, color, size, 0,
                         far ? ParticleFlag::Back : ParticleFlag::None);
            }
        }
        lastTear = std::max(lastTear, k * 1.15);
        for (size_t left = wisps.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        w = wisps[i];
            w.age += dt;
            if (w.age > w.life) {
                wisps.erase(wisps.begin() + std::ptrdiff_t(i));
                continue;
            }
            w.vy -= 30 * dt;
            w.vx += std::sin(w.age * 9 + w.seed) * 120 * dt;
            w.vx -= w.vx * 2 * dt;
            w.vy -= w.vy * 1.2 * dt;
            w.x += w.vx * dt;
            w.y += w.vy * dt;
            w.points.push_back({w.x, w.y});
            if (w.points.size() > 14) w.points.erase(w.points.begin());
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void blade(Paint& p, int half) const {
        const double U = A.span * head(t), k = tear();
        if (U < .02 || k >= 1) return;
        const auto ramp = half < 0 ? std::span<const uint32_t>(dim) : std::span<const uint32_t>(C);
        melee::smear(
            p, A, 0, U, 21,
            [&](double u, double r, ShadeOutput& out, double) {
                const double v = u / U, w = 21 * std::pow(std::sin(PI * std::pow(v, 1.7)), .55);
                if (w < .5) return;
                const double q = (A.R - r) / w;
                if (q < 0 || q > 1) return;
                const double n = rand({jsUint(std::floor(u * 40)), jsUint(std::floor(r)), 7});
                if (k > 0 && u / A.span < k * 1.2 - n * .2) return;
                const size_t ci = q < .08 ? 7 : q < .16 ? 6 : q < .25 ? 5 : q < .4 ? 4 : q < .62 ? 3 : 2;
                out.c           = ramp[t > T1 && q < .2 ? ci - 1 : ci];
                out.a           = std::min(1., v / .25);
            },
            half);
        melee::glowAlong(
            p, A, A.R - 2, std::max(0., k * 1.2 * A.span - .3), U, 14, C[5], [&](double u) { return .3 * (u / U); },
            half, .25);
    }
    void threads(Paint& p, bool back) const {
        for (const auto& w : wisps) {
            if (w.far != back) continue;
            const double life = 1 - w.age / w.life;
            for (size_t i = 1; i < w.points.size(); ++i) {
                const double f = double(i) / double(w.points.size());
                const auto   a = w.points[i - 1], b = w.points[i];
                p.line(a.x, a.y, b.x, b.y, f > .75 ? C[6] : f > .4 ? C[5] : C[3], life * (.3 + .7 * f));
            }
            if (life > .5) p.glow(w.x, w.y, 5, C[5], .25 * life);
        }
    }
    void drawBack(Paint& p) const override {
        blade(p, -1);
        ps.draw(p, Paint::Layer::Back);
        threads(p, true);
    }
    void draw(Paint& p) const override {
        blade(p, 1);
        if (t < WIND + .05) {
            const auto a = melee::onSwing(A, A.R - 3, 0);
            p.sparkle(a.x, a.y, 6 * std::min(1., t / .04) * (1 - smooth(WIND - .02, WIND + .05, t)), C[6], C[5], .45);
        }
        const double g = t - T1 + .06;
        if (g > 0 && g < .3) {
            const auto a = melee::onSwing(A, A.R - 3, A.span);
            p.sparkle(a.x, a.y, 9 * ease::outBack(std::min(1., g / .06)) * (1 - g / .3), C[6], C[5], .55);
        }
        ps.draw(p, Paint::Layer::Front);
        threads(p, false);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& scytheReapDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ScytheReap, "melee/scythe-reap", 184, 116, 84, 98, 2.1, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 13;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x07040c), hex(0x140a22), hex(0x24113a), hex(0x3b1d58),
                              hex(0x5a3a82), hex(0x7fd6c0), hex(0xd8fff2), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ScytheReap>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
