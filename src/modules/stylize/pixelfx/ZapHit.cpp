// eve-hot-path
// Native adaptation of pixelfx lightning/zap-hit.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Lightning.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SNAP = .05;
struct BurnPoint {
    double x, y, d;
};
struct Branch {
    std::vector<BurnPoint> pts;
    double                 w;
};
void grow(std::vector<Branch>& out, Rng& R, double x, double y, double a, double len, double w, double d, int depth) {
    std::vector<BurnPoint> pts{{x, y, d}};
    int                    side = R.sign();
    for (double s = 0; s < len; s += 1.4) {
        a += R.range(-.3, .3);
        x += std::cos(a) * 1.4;
        y += std::sin(a) * 1.4;
        d += 1.4;
        pts.push_back({x, y, d});
        if (depth < 2 && s > 2 && R.isChance(.24)) {
            side               = -side;
            const double angle = a + side * R.range(.55, 1.05), l = (len - s) * R.range(.3, .55);
            grow(out, R, x, y, angle, l, w * .6, d, depth + 1);
        }
    }
    out.push_back({std::move(pts), w});
}
struct Spark {
    double             x = 0, y = 0, vx, vy, life, age = -SNAP, next;
    std::vector<Point> trail;
};
struct ZapHit final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              seed;
    Rng                   R;
    Particles             ps;
    Lights                lights;
    std::vector<Branch>   tree;
    std::array<Spark, 16> kinked{};
    double                Y0, t = 0;
    bool                  hit = false;
    explicit ZapHit(const PixelFxSettings& o) : C(o.palette), seed(o.seed), R(o.seed) {
        for (int i = 0; i < 6; ++i) {
            const double a = (i / 6.) * TAU + R.range(-.35, .35), l = R.range(18, 32);
            grow(tree, R, 0, 0, a, l, 1, 0, 0);
        }
        Y0 = R.range(-14, 6);
        for (auto& s : kinked) {
            const double heading = R.range(-1.2, 1.2), a = heading + (R.isChance(.7) ? 0 : PI), sp = R.range(130, 250);
            s.vx   = std::cos(a) * sp;
            s.vy   = std::sin(a) * sp - 40;
            s.life = R.range(.2, .36);
            s.next = R.range(.02, .05);
        }
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= SNAP) {
            hit = true;
            lights.add(0, 0, 60, C[4], .7, .3);
            lights.add(0, 0, 14, WHITE, .5, .07);
            for (int i = 0; i < 3; ++i) {
                const double x = R.range(-3, 5), y = R.range(-4, 2), vx = R.range(-5, 8), vy = -R.range(8, 16),
                             life = R.range(.55, .75), size = R.range(2.5, 3.8), delay = R.range(.08, .16);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, mix(hex(0x6a6680), C[4], .3), size, delay);
            }
        }
        for (auto& s : kinked) {
            s.age += dt;
            if (s.age < 0 || s.age >= s.life) continue;
            s.next -= dt;
            if (s.next <= 0) {
                s.next          = R.range(.025, .055);
                const double sp = jsHypot2(s.vx, s.vy), a = std::atan2(s.vy, s.vx) + R.signedRange(.6, 1.3);
                s.vx = std::cos(a) * sp;
                s.vy = std::sin(a) * sp;
                s.trail.push_back({s.x, s.y});
                if (s.trail.size() > 4) s.trail.erase(s.trail.begin());
            }
            s.vx -= s.vx * 3 * dt;
            s.vy -= s.vy * 3 * dt - 140 * dt;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
        }
        ps.update(dt, R);
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        if (t < SNAP) return;
        const double cool = smooth(.45, .85, t), fade = 1 - smooth(.78, .88, t);
        for (const auto& br : tree)
            for (size_t i = 1; i < br.pts.size(); ++i) {
                const auto   a = br.pts[i - 1], b = br.pts[i];
                const double age = t - SNAP - b.d / 260;
                if (age < 0) break;
                const double k = std::exp(-age / .22) - cool * .3;
                const auto   c = k > .75    ? WHITE
                                 : k > .45  ? C[6]
                                 : k > .25  ? C[5]
                                 : k > .1   ? C[4]
                                 : k > -.05 ? C[2]
                                            : C[1];
                p.line(a.x, a.y, b.x, b.y, c, fade);
                if (br.w >= 1 && b.d < 9) p.line(a.x + 1, a.y, b.x + 1, b.y, k > .45 ? C[5] : C[2], fade);
                if ((i & 3u) == 0 && k > 0) p.glow(b.x, b.y, 5, C[5], .35 * k * fade);
            }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const uint32_t f = jsUint(std::floor(t * 30 + 1e-4));
        if (t < .32) {
            Rng          r(hash({seed, 2, f}));
            auto         b = lightning::zigzag(r, -66, Y0, 0, 0, .22, .35);
            const double u = clamp(t / SNAP), k = t < SNAP ? 1 : t < .2 ? (f % 2 ? .7 : 1) : .55 * (1 - (t - .2) / .12);
            lightning::Stroke options;
            options.upTo = u;
            options.hero = t >= SNAP && t < SNAP + .034;
            auto paths   = u < 1 ? std::vector<lightning::Path>{b[0]} : b;
            lightning::stroke(p, paths, k, C, t < SNAP + .034 ? .95 : .7, options);
            if (u < 1) {
                const auto h = b[0].pts[size_t(std::floor(u * double(b[0].pts.size() - 1)))];
                p.circle(h.x, h.y, 2, WHITE);
                p.glow(h.x, h.y, 14, C[5], .6);
            }
        }
        if (t >= SNAP && t < .45) {
            const double a = (t - SNAP) / .4, R0 = 8 * (1 - a * .6), m = jsRound(11 * (1 - a * .5));
            Rng          r(hash({seed, 5, f}));
            for (int i = 0; i < m; ++i) {
                const double ang = r.range(0, TAU), d0 = r.range(.5, R0 * .5), ang2 = ang + r.range(-1.2, 1.2),
                             d1 = r.range(R0 * .55, R0);
                const auto   b  = lightning::zigzag(r, std::cos(ang) * d0, std::sin(ang) * d0, std::cos(ang2) * d1,
                                                    std::sin(ang2) * d1, .5, 0);
                lightning::crackle(p, b, .75 * (1 - a) + .25, C, .25);
            }
            p.circle(0, 0, 3 * (1 - a) + 1, C[5]);
            p.circle(0, 0, 2 * (1 - a) + .5, WHITE);
            p.glow(0, 0, 20, C[4], .55 * (1 - a));
            if (t < SNAP + .07) p.glow(0, 0, 9, WHITE, .4);
        }
        for (const auto& s : kinked) {
            if (s.age < 0 || s.age >= s.life) continue;
            const double k    = 1 - s.age / s.life;
            auto         path = s.trail;
            path.push_back({s.x, s.y});
            for (size_t i = 0; i + 1 < path.size(); ++i)
                p.line(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y,
                       i + 2 == path.size() ? (k > .5 ? WHITE : C[6])
                       : k > .5             ? C[5]
                                            : C[4],
                       .6 + .4 * (double(i) / double(path.size())));
            p.px(s.x, s.y, k > .4 ? WHITE : C[5]);
            p.glow(s.x, s.y, 4, C[5], .4 * k);
        }
        if (t >= SNAP)
            p.glow(0, 0, 40 * std::max(0., 1 - t / .85), C[3], .45 * std::exp(-(t - SNAP) * 5) * (f % 2 ? .7 : 1));
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& zapHitDescriptor() {
    static const Descriptor d{
        {PixelFxKind::ZapHit, "lightning/zap-hit", 144, 112, 76, 56, .9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 3;
            s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ZapHit>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
