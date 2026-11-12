// eve-hot-path
// Native adaptation of pixelfx status/petrified.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/StatusShape.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CREEP = .95, CRACK = 1.75, SHATTER = 2.2;
struct StonePixel {
    int      x, y;
    double   up;
    uint32_t color;
};
struct Petrified final : Kernel {
    const std::vector<uint32_t>     C;
    const PixelFxActor              actor;
    const status::Shape             shape;
    const uint32_t                  seed;
    const double                    ox, oy;
    Rng                             R;
    std::vector<StonePixel>         stone;
    std::vector<std::vector<Point>> cracks;
    Particles                       ps;
    Bits                            bits;
    Lights                          lights;
    double                          t     = 0;
    bool                            broke = false, settled = false;
    explicit Petrified(const PixelFxSettings& o)
        : C(o.palette),
          actor(o.actor),
          shape(actor),
          seed(o.seed),
          ox(jsRound(shape.width * .55)),
          oy(jsRound(shape.height * .42)),
          R(seed) {
        ps.sparks = {C[7], C[6], C[5], C[4]};
        for (const auto& q : shape.body) {
            const int    x = q[0], y = q[1];
            const double tone   = clamp((luma(status::colorAt(actor, x, y)) - .06) / .78),
                         mottle = tileFbm(x * .32, y * .32, 0, 0, seed + 5u, 2);
            int          i      = 1 + int(jsRound(tone * 4.6 + mottle * 1.1));
            if (!shape.isOccupied(x - 1, y) || !shape.isOccupied(x, y - 1))
                ++i;
            else if (!shape.isOccupied(x + 1, y) || !shape.isOccupied(x, y + 1))
                --i;
            stone.push_back({x, y, 1 - (y + .5) / shape.height, C[size_t(std::clamp(i, 1, 6))]});
        }
        for (int i = 0; i < 7; ++i) {
            const double angle = (double(i) / 7) * TAU + R.range(-.3, .3);
            walk(ox, oy, angle, 14);
        }
    }
    void walk(double x, double y, double angle, double length) {
        std::vector<Point> points{{x, y}};
        for (int s = 0; s < length; ++s) {
            angle += R.range(-.55, .55);
            x += std::cos(angle) * R.range(1.4, 2.4);
            y += std::sin(angle) * R.range(1.4, 2.4);
            if (!shape.isOccupied(int(std::floor(x)), int(std::floor(y)))) break;
            points.push_back({x, y});
            if (points.size() > 3 && cracks.size() < 16 && R.isChance(.14)) {
                const double branch = angle + R.signedRange(.6, 1.2);
                walk(x, y, branch, length / 2);
            }
        }
        cracks.push_back(std::move(points));
    }
    double front(int x) const {
        const double level = t >= CREEP ? 1.2 : 1.2 * smooth(0, CREEP, t) + .025 * std::sin((t / CREEP) * TAU * 3);
        return level + .06 * tileFbm(x * .4, .5, 0, 0, seed, 2);
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < CREEP ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    void update(double dt) override {
        t += dt;
        const auto&  a = actor;
        const double H = shape.height;
        if (t < CREEP)
            for (int n = 0; n < 60 * dt; ++n) {
                if (!R.isChance(.5)) continue;
                const auto q  = shape.body[size_t(R.integer(0, int(shape.body.size()) - 1))];
                const int  mx = q[0], my = q[1];
                if (std::abs(1 - (my + .5) / H - front(mx)) > .04) continue;
                const double x = a.x + mx + .5, y = a.y + my, side = mx < double(shape.width) / 2 ? -1. : 1.;
                const double vx = side * R.range(15, 60), vy = -R.range(30, 90), life = R.range(.6, 1.1);
                const auto   color = C[size_t(R.integer(4, 6))];
                const double size  = R.isChance(.3) ? 2. : 1.;
                ps.spawn(R, ParticleKind::Grit, x, y, vx, vy, life, color, size);
                if (R.isChance(.3)) {
                    const double vx0 = side * R.range(10, 30), vy0 = -R.range(4, 14), life0 = R.range(.4, .7),
                                 size0 = R.range(1.5, 2.6);
                    ps.spawn(R, ParticleKind::Dust, x + side * 2, y, vx0, vy0, life0, C[4], size0);
                }
                if (R.isChance(.25)) {
                    const double vx0 = side * R.range(40, 90), vy0 = -R.range(20, 70), life0 = R.range(.15, .3);
                    ps.spawn(R, ParticleKind::Mote, x, y, vx0, vy0, life0, C[7], .5);
                }
            }
        if (!settled && t >= CREEP) {
            settled = true;
            lights.add(0, a.y + H * .5, 34, C[6], .35, .3);
            for (int i = 0; i < 8; ++i) {
                const double x = R.range(-8, 8), y = -R.range(0, 2), vx = R.signedRange(20, 50), vy = -R.range(2, 10),
                             life = R.range(.6, 1), size = R.range(2, 3.5);
                ps.spawn(R, ParticleKind::Dust, x, y, vx, vy, life, C[3], size);
            }
        }
        if (t > CREEP + .2 && t < SHATTER && R.isChance(1.6 * dt)) {
            const auto q = shape.body[size_t(R.integer(0, int(shape.body.size()) - 1))];
            if (!shape.isOccupied(q[0], q[1] + 1)) {
                const double vx = R.range(-8, 8), life = R.range(.8, 1.2);
                ps.spawn(R, ParticleKind::Grit, a.x + q[0] + .5, a.y + q[1] + 1, vx, 0, life, C[3], 1);
            }
        }
        if (t >= CRACK && t < SHATTER && R.isChance(12 * dt)) {
            const auto&  points = cracks[size_t(R.integer(0, int(cracks.size()) - 1))];
            const auto   q      = points[size_t(std::floor(R.next() * double(points.size())))];
            const double vx = R.range(-40, 40), vy = -R.range(30, 80), life = R.range(.5, .9);
            const auto   color = C[size_t(R.integer(5, 6))];
            ps.spawn(R, ParticleKind::Grit, a.x + q.x, a.y + q.y, vx, vy, life, color, 1);
        }
        if (!broke && t >= SHATTER) {
            broke = true;
            lights.add(0, a.y + H * .45, 46, C[6], .7, .5);
            lights.add(0, a.y + H * .45, 30, WHITE, .7, .12);
            const double cx = a.x + ox, cy = a.y + oy;
            for (int i = 0; i < 18; ++i) {
                const auto   q = stone[size_t(R.integer(0, int(stone.size()) - 1))];
                const double x = a.x + q.x + .5, y = a.y + q.y + .5, dx = x - cx, dy = y - cy, dist = jsHypot2(dx, dy),
                             d = dist != 0 ? dist : 1, sp = R.range(35, 110);
                Debris       b;
                b.kind            = DebrisKind::Chunk;
                b.x               = x;
                b.y               = y;
                b.vx              = (dx / d) * sp;
                b.vy              = (dy / d) * sp * .6 - R.range(60, 160);
                b.gravity         = 1;
                b.drag            = .4;
                b.bounce          = .3;
                b.life            = R.range(1.1, 1.5);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-14, 14);
                b.size            = R.range(1.3, 2.8);
                b.ramp            = {C[0], C[2], C[3], C[5], C[6]};
                bits.add(std::move(b));
            }
            for (const auto& q : stone) {
                if (rand({uint32_t(q.x), uint32_t(q.y), 9u}) > .22) continue;
                const double x = a.x + q.x + .5, y = a.y + q.y + .5, dx = x - cx, dy = y - cy, dist = jsHypot2(dx, dy),
                             d = dist != 0 ? dist : 1, sp = R.range(40, 130), vx = (dx / d) * sp,
                             vy = (dy / d) * sp * .6 - R.range(40, 140), life = R.range(.9, 1.4),
                             size = R.isChance(.35) ? 2. : 1.;
                ps.spawn(R, ParticleKind::Grit, x, y, vx, vy, life, q.color, size);
            }
            for (int i = 0; i < 16; ++i) {
                const double angle = -PI / 2 + R.range(-1.4, 1.4), sp = R.range(50, 120);
                Debris       b;
                b.kind            = DebrisKind::Shard;
                b.x               = cx + R.range(-5, 5);
                b.y               = cy + R.range(-8, 8);
                b.vx              = std::cos(angle) * sp;
                b.vy              = std::sin(angle) * sp;
                b.gravity         = .8;
                b.drag            = .6;
                b.bounce          = .3;
                b.life            = R.range(.9, 1.4);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-18, 18);
                b.size            = R.range(1.5, 3);
                b.color           = C[size_t(R.integer(5, 7))];
                bits.add(std::move(b));
            }
            for (int i = 0; i < 6; ++i) {
                const double x = cx + R.range(-12, 12), y = cy + R.range(-10, 14), vx = R.range(-10, 10),
                             vy = -R.range(5, 20), life = R.range(.4, .9), size = R.range(1, 2.2),
                             delay = R.range(.05, .4);
                ps.spawn(R, ParticleKind::Star, x, y, vx, vy, life, C[6], size, delay);
            }
            for (int i = 0; i < 12; ++i) {
                const double side = R.sign(), x = side * R.range(0, 8), y = -R.range(1, 6), vx = side * R.range(20, 60),
                             vy = -R.range(4, 16), life = R.range(.9, 1.4), size = R.range(2.4, 4),
                             delay = R.range(0, .08);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, C[4], size, delay);
            }
            for (int i = 0; i < 6; ++i) {
                const double x = R.range(-6, 6), y = a.y + H * R.range(.3, .8), vx = R.range(-20, 20),
                             vy = -R.range(10, 30), life = R.range(1, 1.5), size = R.range(3, 4.5),
                             delay = R.range(0, .1);
                ps.spawn(R, ParticleKind::Smoke, x, y, vx, vy, life, C[3], size, delay);
            }
        }
        bits.update(dt, [](double) { return 0.; });
        for (auto& b : bits.list)
            if (b.rest && !b.dusted) {
                b.dusted        = true;
                const double vx = R.range(-20, 20), vy = -R.range(2, 10), life = R.range(.3, .6),
                             size = R.range(1.2, 2.2);
                ps.spawn(R, ParticleKind::Dust, b.x, -.5, vx, vy, life, C[3], size);
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { ps.draw(p, Paint::Layer::Back); }
    void draw(Paint& p) const override {
        const auto& a = actor;
        if (t < SHATTER) {
            const double sh =
                t >= CRACK ? jsRound(std::sin(t * 90) * std::min(1., (t - CRACK) / (SHATTER - CRACK)) * 1.2) : 0;
            const bool white = t > SHATTER - .035;
            for (const auto& q : stone) {
                const double d = front(q.x) - q.up;
                if (d < 0) continue;
                const double x = a.x + q.x + sh, y = a.y + q.y;
                if (d < .035 && t < CREEP)
                    p.px(x, y, rand({uint32_t(q.x), uint32_t(q.y), uint32_t(std::floor(t * 24))}) < .5 ? C[7] : C[6]);
                else if (d < .07 && t < CREEP)
                    p.px(x, y, C[5]);
                else
                    p.px(x, y, white ? WHITE : q.color);
            }
            if (t < CREEP)
                for (int mx = 0; mx < shape.width; mx += 2) {
                    const int my = int(std::floor((1 - front(mx)) * shape.height));
                    if (shape.isOccupied(mx, my)) p.glow(a.x + mx, a.y + my, 5, C[6], .18);
                }
            if (t >= CRACK) {
                const double k = clamp((t - CRACK) / .3);
                for (const auto& points : cracks) {
                    const int n = int(std::floor(double(points.size()) * k));
                    for (int i = 0; i < n; ++i) {
                        const auto   q = points[size_t(i)];
                        const double x = std::floor(a.x + q.x) + sh, y = std::floor(a.y + q.y);
                        const bool   fresh = i > n - 3;
                        p.px(x, y, fresh ? WHITE : C[0]);
                        if (!fresh) p.px(x, y - 1, C[6]);
                    }
                }
                p.glow(a.x + ox, a.y + oy, 12, C[6], .25 * k);
            }
        }
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& petrifiedDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Petrified, "status/petrified", 120, 74, 60, 64, 3.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 3;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x16141c), hex(0x2a2733), hex(0x433f4d), hex(0x5f5a6a),
                              hex(0x7d7889), hex(0xa29dab), hex(0xcdc8d2), hex(0xf0edf3)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& o) -> std::unique_ptr<Kernel> { return std::make_unique<Petrified>(o); },
        nullptr,
        nullptr,
        true,
        1};
    return d;
}
}  // namespace eve::stylize::pixelfx
