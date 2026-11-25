// eve-hot-path
// Native adaptation of pixelfx ice/ice-spikes.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Ice.h"
#include "stylize/pixelfx/IcePanes.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double START = .04, RUN = .44, SHIMMER = .74, TREMBLE = 1.2, FLASH = 1.36, SHATTER = 1.42, MELT = 1.75, D = 3;
struct Blade {
    double dx, h, w, lean;
    bool   back;
    double delay, jag;
    bool   main = false;
};
struct Cluster {
    double             x, q, H, W, t0;
    std::vector<Blade> blades;
    bool               up = false;
};
struct Pose {
    double g, x, tx, ty;
    bool   white;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size = 1, delay = 0;
};
struct Chip {
    DebrisKind            kind;
    double                x, y, vx, vy, g, drag, bounce, life, a, va, s;
    uint32_t              c;
    std::vector<uint32_t> ramp{};
};
struct IceSpikes final : Kernel {
    std::vector<uint32_t> C;
    Rng                   R;
    double                reach, t = 0;
    Particles             ps;
    Bits                  bits;
    Lights                lights;
    ice::Panes            panes;
    std::vector<Cluster>  clusters;
    size_t                crest = 0;
    bool                  broke = false, trembled = false;
    explicit IceSpikes(const PixelFxSettings& o) : C(o.palette), R(o.seed), reach(o.ice.reach) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        for (double x = 10; x < reach - 2; x += R.range(20, 26)) {
            const double       q = x / reach, v = (q - .64) / .3,
                               H = o.ice.height * (.28 + .72 * std::exp(-(v * v))) * R.range(.88, 1.06),
                               W = clamp(H * .36, 5, 14), lean = H * R.range(.2, .34);
            std::vector<Blade> list;
            list.push_back({W * R.range(.35, .6), H * R.range(.5, .7), W * .7, H * R.range(.4, .6), true,
                            R.range(.02, .05), R.isChance(.5) ? R.range(.35, .6) : -1});
            list.push_back({0, H, W, lean, false, 0, R.isChance(.7) ? R.range(.32, .58) : -1, true});
            if (H > 14)
                list.push_back({-W * R.range(.35, .55), H * R.range(.24, .4), W * .55, -H * R.range(0, .12), false,
                                R.range(.03, .07), -1});
            clusters.push_back({x, q, H, W, reachedAt(x) + .012, std::move(list)});
            if (H > clusters[crest].H) crest = clusters.size() - 1;
        }
    }
    double front() const { return reach * (1 - std::pow(1 - seg(t, START, START + RUN), 1.7)); }
    double reachedAt(double x) const { return START + RUN * (1 - std::pow(1 - clamp(x / reach), 1 / 1.7)); }
    Pose   pose(const Cluster& c, const Blade& b) const {
        const double g = ease::outBack(clamp((t - c.t0 - b.delay) / .09), 2.2), x = c.x + b.dx;
        return {g, x, x + b.lean * g, -b.h * g, t - c.t0 - b.delay < .03 || (t >= FLASH && t < SHATTER)};
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void chip(Chip v) {
        Debris q;
        q.kind            = v.kind;
        q.x               = v.x;
        q.y               = v.y;
        q.vx              = v.vx;
        q.vy              = v.vy;
        q.gravity         = v.g;
        q.drag            = v.drag;
        q.bounce          = v.bounce;
        q.life            = v.life;
        q.angle           = v.a;
        q.angularVelocity = v.va;
        q.size            = v.s;
        q.color           = v.c;
        q.ramp            = std::move(v.ramp);
        bits.add(std::move(q));
    }
    void splinter(double x, double y, double vx, int n) {
        for (int i = 0; i < n; ++i)
            chip({DebrisKind::Shard, x + R.range(-2, 2), y - 1, vx * .5 + R.range(-45, 45), -R.range(30, 100), .8, .6,
                  .35, R.range(.4, .75), R.range(0, TAU), R.range(-18, 18), R.range(1.2, 2.4),
                  C[size_t(R.integer(4, 7))]});
    }
    void breakBlade(const Cluster& c, const Blade& b) {
        const auto   s  = pose(c, b);
        const double ax = s.tx - s.x, ay = s.ty, h = jsHypot2(ax, ay), L = h == 0 ? 1 : h, nx = -ay / L, ny = ax / L,
                     hw = b.w / 2, ang = std::atan2(ay, ax) + PI / 2;
        std::vector<std::vector<Point>> parts{{{s.x - nx * hw, -ny * hw}, {s.tx, s.ty}, {s.x + nx * hw, ny * hw}}};
        const auto                      cut = [&](double u, bool upper) {
            std::vector<std::vector<Point>> out;
            for (auto& poly : parts) {
                if (upper && !std::any_of(poly.begin(), poly.end(), [&](Point q) { return q.y < ay * .5; })) {
                    out.push_back(std::move(poly));
                    continue;
                }
                auto split = ice::splitPoly(poly, s.x + ax * u, ay * u, ang);
                for (auto& q : split) out.push_back(std::move(q));
            }
            parts = std::move(out);
        };
        if (b.h > 9) cut(.4, false);
        if (b.h > 22) cut(.72, true);
        for (const auto& part : parts) {
            double mx = 0, my = 0;
            for (const auto& q : part) {
                mx += q.x;
                my += q.y;
            }
            mx /= double(part.size());
            my /= double(part.size());
            const double up = clamp(-my / 40);
            ice::Pane    q;
            for (const auto& p : part) q.pts.push_back({p.x - mx, p.y - my});
            q.x       = mx;
            q.y       = my;
            q.vx      = R.range(-25, 60);
            q.vy      = -R.range(40, 130) * (.5 + up);
            q.a       = 0;
            q.va      = R.range(-8, 8);
            q.f       = R.range(-.7, .7);
            q.vf      = R.range(-6, 6);
            q.lit     = b.back ? .45 : .75;
            q.g       = .9;
            q.drag    = .6;
            q.bounce  = .3;
            q.solid   = true;
            q.ridge   = std::atan2(ay, ax);
            q.brittle = b.h > 30 ? 2 : 1;
            q.crumble = R.range(.12, .4);
            q.life    = R.range(1, 1.4);
            panes.add(std::move(q));
        }
        for (int i = 0; i < 2 + b.h / 7; ++i) {
            const double u = R.next();
            chip({R.isChance(.15) ? DebrisKind::Chunk : DebrisKind::Shard,
                  s.x + ax * u + R.range(-1.5, 1.5),
                  ay * u,
                  R.range(-60, 90),
                  -R.range(60, 230),
                  .9,
                  .6,
                  .3,
                  R.range(.7, 1.3),
                  R.range(0, TAU),
                  R.range(-20, 20),
                  R.range(1.2, 3.2),
                  C[size_t(R.integer(4, 7))],
                  {C[1], C[3], C[4], C[6], C[7]}});
        }
    }
    void update(double dt) override {
        t += dt;
        if (t - dt <= START && t > START) {
            for (int i = 0; i < 4; ++i)
                emit({ParticleKind::Vapour, R.range(0, 6), -R.range(0, 3), R.range(10, 60), -R.range(2, 12),
                      R.range(.4, .7), C[6], R.range(2, 3.2)});
            lights.add(4, -4, 26, C[5], .6, .25);
        }
        const double fr = front();
        if (t > START && fr < reach - 1) {
            if (R.isChance(.5))
                emit({ParticleKind::Streak, fr - R.range(4, 12), -R.range(1, 3), R.range(40, 80), -R.range(0, 6),
                      R.range(.08, .14), R.isChance(.5) ? C[6] : WHITE});
            if (R.isChance(.3))
                emit({ParticleKind::Flake, fr, -1, R.range(10, 50), -R.range(20, 70), R.range(.5, .9),
                      R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.2)});
            if (R.isChance(.12))
                emit({ParticleKind::Vapour, fr, -R.range(0, 3), R.range(20, 60), -R.range(2, 10), R.range(.4, .7), C[6],
                      R.range(1.6, 2.8)});
        }
        for (size_t index = 0; index < clusters.size(); ++index) {
            auto& c = clusters[index];
            if (c.up || t < c.t0) continue;
            c.up        = true;
            const int n = 3 + int(jsRound(c.H / 8));
            for (int i = 0; i < n; ++i) {
                const double a = -PI / 2 + R.range(-1.1, 1), sp = R.range(60, 190) * (.45 + c.H / 44);
                chip({DebrisKind::Shard, c.x + R.range(-3, 3), -1, std::cos(a) * sp + 25, std::sin(a) * sp, .8, .6, .3,
                      R.range(.6, 1.1), R.range(0, TAU), R.range(-18, 18), R.range(1.2, 3),
                      C[size_t(R.integer(5, 7))]});
            }
            emit({ParticleKind::Vapour, c.x, -1, R.range(-10, 40), -R.range(2, 10), R.range(.5, .9), C[6],
                  R.range(2.2, 3.4) * (.6 + c.H / 60)});
            for (int i = 0; i < 2; ++i)
                emit({ParticleKind::Flake, c.x + R.range(-4, 4), -R.range(2, c.H * .5), R.range(-20, 40),
                      -R.range(20, 60), R.range(.6, 1.1), R.isChance(.5) ? WHITE : C[6], R.range(.5, 1.3)});
            if (c.H > 16)
                emit({ParticleKind::Star, c.x + c.blades[1].lean, -c.H * .96, 0, -4, R.range(.3, .5), WHITE,
                      R.range(1.4, 2.6), .08});
            lights.add(c.x, -c.H * .4, 16 + c.H * .6, C[5], .5, .18);
            if (index == crest) {
                lights.add(c.x, -c.H * .5, 54, WHITE, .5, .14);
                for (int i = 0; i < 4; ++i)
                    emit({ParticleKind::Vapour, c.x + R.range(-6, 6), -R.range(0, 4), R.range(-40, 60), -R.range(4, 16),
                          R.range(.6, 1), C[6], R.range(3, 4.5)});
            }
        }
        if (t > START + RUN && t < TREMBLE && R.isChance(.09)) {
            const auto&  c = clusters[size_t(R.integer(0, int(clusters.size()) - 1))];
            const auto&  b = c.blades[size_t(R.integer(0, int(c.blades.size()) - 1))];
            const auto   s = pose(c, b);
            const double u = R.range(.3, .95);
            emit({ParticleKind::Star, s.x + (s.tx - s.x) * u, s.ty * u, 0, -3, R.range(.25, .5),
                  R.isChance(.6) ? WHITE : C[6], R.range(.6, 2)});
        }
        if (!trembled && t >= TREMBLE) {
            trembled = true;
            for (const auto& c : clusters)
                for (int i = 0; i < 2; ++i)
                    chip({DebrisKind::Shard, c.x + R.range(-4, 4), -R.range(4, c.H * .8), R.range(-20, 20),
                          -R.range(0, 30), .8, .5, .3, R.range(.3, .6), R.range(0, TAU), R.range(-18, 18),
                          R.range(1, 1.8), C[size_t(R.integer(5, 7))]});
        }
        if (!broke && t >= SHATTER) {
            broke = true;
            for (const auto& c : clusters) {
                for (const auto& b : c.blades) breakBlade(c, b);
                for (int i = 0; i < 2; ++i)
                    emit({ParticleKind::Vapour, c.x + R.range(-6, 6), -R.range(1, 8), R.range(-30, 50), -R.range(4, 18),
                          R.range(.6, 1.1), C[6], R.range(2.4, 4) * (.7 + c.H / 60)});
                if (R.isChance(.7))
                    emit({ParticleKind::Star, c.x + R.range(-6, 6), -R.range(4, c.H), R.range(-10, 10), -R.range(6, 20),
                          R.range(.4, .8), R.isChance(.5) ? WHITE : C[6], R.range(1, 2.2)});
                for (int i = 0; i < 2; ++i)
                    emit({ParticleKind::Flake, c.x + R.range(-6, 6), -R.range(4, c.H), R.range(-30, 60),
                          -R.range(30, 90), R.range(.9, 1.5), R.isChance(.6) ? WHITE : C[6], R.range(.5, 1.5)});
                lights.add(c.x, -c.H * .5, 22 + c.H, WHITE, .45, .12);
            }
            for (int i = 0; i < 5; ++i) lights.add((reach * (i + .5)) / 5, -18, 40, C[4], .4, .5);
        }
        if (broke && t < SHATTER + .8 && R.isChance(.1) && !bits.list.empty()) {
            const auto& b = bits.list[size_t(R.integer(0, int(bits.list.size()) - 1))];
            emit({ParticleKind::Star, b.x, b.y - 1, 0, -3, R.range(.2, .4), R.isChance(.5) ? WHITE : C[6],
                  R.range(.6, 1.5)});
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        bits.update(dt, [](double) { return 0.; });
        panes.update(
            dt, [](double) { return 0.; },
            [&](const ice::Pane& q, double x, double y, double speed) {
                if (speed < 110) return;
                emit({ParticleKind::Vapour, x, y - 1, R.range(-20, 20), -R.range(2, 8), R.range(.4, .7), C[6],
                      R.range(1.4, 2.4)});
                splinter(x, y, q.vx, 3);
                if (R.isChance(.3))
                    emit({ParticleKind::Star, x, y - 2, 0, -5, R.range(.2, .4), WHITE, R.range(.8, 1.6)});
            },
            [&](const ice::Pane& q) {
                splinter(q.x, 0, 0, 3);
                emit({ParticleKind::Vapour, q.x, -1, R.range(-12, 12), -R.range(2, 8), R.range(.4, .7), C[6],
                      R.range(1.4, 2.2)});
            },
            [](const ice::Pane&, double, double) {});
        lights.update(dt);
        const double left = std::max(.001, D - .05 - t);
        for (auto& q : ps.list)
            if (q.life > left) q.life = left;
        for (auto& q : bits.list)
            if (q.life > left) q.life = left;
        for (auto& q : panes.list)
            if (q.life > left) q.life = left;
    }
    void drawFrost(Paint& p) const {
        const double fr = front();
        for (int x = -3; x <= std::min(fr, reach + 6); ++x) {
            const double q = clamp(x / reach), age = t - reachedAt(std::max(0, x)),
                         a = 1 - smooth(MELT + q * .35, MELT + q * .35 + .6, t);
            if (a <= 0 || age < 0) continue;
            const double h = rand({jsUint(x), 17}), grow = smooth(0, .3, age);
            const bool   head = fr - x < 3 && fr < reach;
            p.dot(x, 0, head || age < .06 || h < .3 ? WHITE : C[6], a);
            const int L = h < .45 ? int(jsRound(grow * (1 + rand({jsUint(x), 5}) * 2.6))) : 0;
            for (int j = 1; j <= L; ++j) p.dot(x, -j, j == L ? WHITE : C[5], a);
            if (L > 1 && rand({jsUint(x), 9}) < .5) p.dot(x + 1, -L + 1, C[6], a);
            const double depth = grow * (2 + rand({jsUint(x), 11}) * 4) * (1 - q * .3);
            for (int j = 1; j <= depth; ++j)
                p.dot(x, j,
                      rand({jsUint(x * 31 + j), 7}) < .06 ? WHITE
                      : j < 2                             ? C[5]
                      : j < 4                             ? C[4]
                                                          : C[3],
                      a * (1.1 - j / (depth + 1)));
            if (head) p.dot(x, -1, WHITE);
        }
        if (fr < reach && t > START) {
            p.glow(fr, -2, 16, C[6], .6);
            p.glow(fr, -1, 7, WHITE, .6);
        }
    }
    void drawBack(Paint& p) const override {
        drawFrost(p);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        if (t < SHATTER)
            for (int pass = 0; pass < 3; ++pass)
                for (const auto& c : clusters) {
                    if (t < c.t0) continue;
                    const double jx =
                        t >= TREMBLE && t < FLASH ? int(std::floor(t * 40) + jsRound(c.x)) % 2 ? 1 : 0 : 0;
                    for (const auto& b : c.blades) {
                        if ((pass == 0) != b.back || (pass == 1) != b.main) continue;
                        const auto s = pose(c, b);
                        if (s.g <= 0) continue;
                        const double      gl = (t - SHIMMER - c.q * .34) / .2;
                        ice::BladeOptions o;
                        o.white = s.white;
                        o.jag   = b.jag;
                        o.dim   = b.back ? 1 : 0;
                        o.glint = gl >= 0 && gl <= 1 ? gl : -1;
                        ice::blade(p, s.x + jx, 0, s.tx + jx, s.ty, b.w, C, o);
                    }
                    if (pass == 2) {
                        const double pop   = clamp(1 - (t - c.t0) / .15);
                        const bool   white = t >= FLASH;
                        p.glow(c.x + c.blades[1].lean * .4, -c.H * .45, c.H * .9 + 10, C[4],
                               (.16 + .42 * pop + (white ? .4 : 0)) * (.85 + .15 * std::sin(t * 3 + c.x)));
                        if (pop > 0) p.glow(c.x, -2, 14, WHITE, .55 * pop);
                    }
                }
        const double k = (1 - smooth(MELT, MELT + .9, t)) * smooth(START, START + .3, t), fr = front();
        for (int i = 0; i < 6; ++i) p.glow((fr * (i + .5)) / 6, -3, 26, C[4], .15 * k);
        if (t >= FLASH && t < SHATTER + .1)
            for (const auto& c : clusters)
                p.sparkle(c.x + c.blades[1].lean, -c.H, 6 * (1 - clamp((t - FLASH) / .16)), C[6], C[4], .4);
        panes.draw(p, C);
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& iceSpikesDescriptor() {
    static const std::array<Option, 2> options{{{"reach", 16, 512, false, 0, &PixelFxIceSettings::reach},
                                                {"height", 4, 128, false, 0, &PixelFxIceSettings::height}}};
    static const Descriptor            d{
        {PixelFxKind::IceSpikes, "ice/ice-spikes", 264, 112, 24, 96, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed       = 3;
            s.ice.reach  = 172;
            s.ice.height = 56;
            s.palette    = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                            hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<IceSpikes>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
