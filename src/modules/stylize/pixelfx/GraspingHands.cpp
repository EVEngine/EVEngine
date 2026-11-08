// eve-hot-path
// Native adaptation of pixelfx shadow/grasping-hands.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RX = 42, OPEN = .32, SHUT = 2.35, LX = -.6, LY = -.8;
struct Snap {
    double x, y, t0;
};
struct Arm {
    double              x, y, length, width, lean, bend, curve, curl, t0, td, phase;
    bool                back, up = false, shut = false, down = false;
    std::optional<Snap> snap = std::nullopt;
};
struct Spine {
    double x, y, angle, width, u;
};
double gripOf(const Arm& a, double t) {
    const double shut = ease::outBack(clamp((t - a.t0 - .3) / .12), 2.2), splay = smooth(a.td, a.td + .12, t);
    return shut * (1 - splay) - .35 * splay;
}
std::optional<std::vector<Spine>> armPose(const Arm& a, double t) {
    const double e = clamp((t - a.t0) / .22), d = clamp((t - a.td) / .34);
    if (e <= 0 || d >= 1) return std::nullopt;
    const double       rise = ease::outBack(e, 1.6) * (1 - ease::inQuad(d)), hidden = (1 - rise) * a.length;
    const double       grip = gripOf(a, t), strain = t > a.t0 + .5 && t < a.td ? std::sin(t * 47 + a.phase) * .03 : 0;
    const double       elbow = a.bend * (.3 + .7 * grip) * (1 - d) + strain,
                       lean  = a.lean + .06 * std::sin(t * 2.2 + a.phase) + d * a.lean * .6;
    double             th = -PI / 2 + lean, x = a.x - std::cos(th) * hidden, y = a.y - std::sin(th) * hidden;
    const int          n = int(std::ceil(a.length / .5));
    std::vector<Spine> pts;
    pts.reserve(size_t(n + 1));
    for (int i = 0; i <= n; ++i) {
        const double s = i * .5, u = s / a.length;
        pts.push_back({x, y, th, a.width * (1 - .5 * u) + .4, u});
        th += .5 *
              (elbow * std::exp(-std::pow((u - .52) / .07, 2)) / (.07 * std::sqrt(PI) * a.length) + a.curve / a.length);
        x += std::cos(th) * .5;
        y += std::sin(th) * .5;
    }
    return pts;
}
void drawArm(Paint& p, const Arm& a, std::span<const Spine> pts, std::span<const uint32_t> C) {
    for (const auto& q : pts) {
        if (q.y > a.y) continue;
        const double nx = -std::sin(q.angle), ny = std::cos(q.angle), side = nx * LX + ny * LY >= 0 ? 1 : -1;
        for (double v = -q.width; v <= q.width; v += .5) {
            const double x = q.x + nx * v, y = q.y + ny * v, u = (v / q.width) * side;
            if (y <= a.y) p.px(x, y, u > .55 ? C[2] : u > -.45 ? C[1] : C[0]);
        }
    }
    for (const auto& q : pts) {
        if (q.y > a.y) continue;
        const double nx = -std::sin(q.angle), ny = std::cos(q.angle), f = nx * LX + ny * LY, side = f >= 0 ? 1 : -1,
                     l = side * q.width;
        if (q.y - ny * l <= a.y) p.px(q.x - nx * l, q.y - ny * l, C[3]);
        if (q.y + ny * l <= a.y) p.px(q.x + nx * l, q.y + ny * l, std::abs(f) > .8 ? C[7] : C[6]);
    }
    const auto base = std::find_if(pts.begin(), pts.end(), [&](const auto& q) { return q.y <= a.y; });
    if (base != pts.end()) p.line(base->x - base->width - 1.5, a.y, base->x + base->width + 1.5, a.y, C[5]);
}
void drawHand(Paint& p, const Arm& a, std::span<const Spine> pts, std::span<const uint32_t> C, double t) {
    const auto& tip = pts.back();
    if (tip.y > a.y - 2) return;
    const double th = tip.angle, dx = std::cos(th), dy = std::sin(th), nx = -dy, ny = dx, g = gripOf(a, t),
                 side = a.curl;
    const double cx = tip.x + dx * 2.6, cy = tip.y + dy * 2.6;
    p.shader(cx - 5, cy - 5, cx + 5, cy + 5, [&](double x, double y, ShadeOutput& out) {
        const double along = (x - cx) * dx + (y - cy) * dy, across = (x - cx) * nx + (y - cy) * ny,
                     d = std::pow(along / 3, 2) + std::pow(across / 2.7, 2);
        if (d > 1.25) return;
        if (d > 1) {
            out.c = across * side < 0 ? C[6] : C[3];
            return;
        }
        out.c = across * side < -1 ? C[2] : C[1];
    });
    const auto bone = [&](double x0, double y0, double angle, double len, bool last) {
        const double x1 = x0 + std::cos(angle) * len, y1 = y0 + std::sin(angle) * len,
                     ox = jsRound(-std::sin(angle) * side * -1), oy = jsRound(std::cos(angle) * side * -1);
        p.line(x0, y0, x1, y1, C[1]);
        p.line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, C[5]);
        if (last) {
            p.px(x1, y1, WHITE);
            p.px(x1 + std::cos(angle), y1 + std::sin(angle), C[7]);
        }
        return Point{x1, y1};
    };
    const double                    spread = 1 - .65 * std::max(0., g), curl = g * 1.2;
    constexpr std::array<double, 4> offsets{-2.2, -.75, .75, 2.2};
    for (size_t i = 0; i < offsets.size(); ++i) {
        const double o = offsets[i], bx = cx + dx * 2.6 + nx * o * .9, by = cy + dy * 2.6 + ny * o * .9,
                     len   = i == 0 || i == 3 ? 2.6 : 3.2;
        const double a0    = th + o * .2 * spread;
        const auto   joint = bone(bx, by, a0 + curl * side * .6, len, false);
        (void)bone(joint.x, joint.y, a0 + curl * side * 1.5, len * .85, true);
    }
    const auto joint = bone(cx - nx * side * 2.4, cy - ny * side * 2.4, th - side * (1 - .6 * g), 2.4, false);
    (void)bone(joint.x, joint.y, th - side * (.6 - 1.4 * g), 2, true);
    p.glow(cx, cy, 8, C[5], .18 + .3 * std::max(0., g));
}
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct PuffEmission {
    double x, y, vx, vy, life, size;
};
struct Splat {
    double x, t0, size;
};
struct GraspingHands final : Kernel {
    const std::vector<uint32_t>     C, DIM;
    Rng                             R;
    Particles                       ps;
    shadow::Puffs                   puffs;
    shadow::Drops                   drops;
    std::array<Arm, 4>              arms{};
    std::vector<shadow::PoolRipple> ripples;
    std::vector<Splat>              splats;
    Lights                          lights;
    double                          t = 0, acc = 0;
    explicit GraspingHands(const PixelFxSettings& o)
        : C(o.palette), DIM{C[0], C[0], C[1], C[1], C[3], C[4], C[5], C[6]}, R(o.seed) {
        ps.sparks   = {C[7], C[6], C[5], C[4]};
        ps.haze     = C[4];
        puffs.flame = {C[7], C[6], C[5], C[4], C[3], C[2], C[1], C[1], C[0]};
        puffs.soot  = {mix(C[3], C[2], .55), mix(C[2], C[1], .5)};
        puffs.cap   = mix(C[4], C[2], .5);
        puffs.rim   = {C[6], C[5], C[4]};
        constexpr std::array<std::array<double, 3>, 4> layout{
            {{-20, -2.5, 1}, {17, -2.5, -1}, {-30, 2.5, 1}, {28, 2.5, -1}}};
        constexpr std::array<int, 4> upOrder{1, 3, 0, 2}, downOrder{2, 0, 3, 1};
        for (size_t i = 0; i < arms.size(); ++i) {
            const auto   v      = layout[i];
            const double toward = v[2];
            arms[i]             = {v[0] + R.range(-3, 3),
                                   v[1],
                                   v[1] < 0 ? R.range(44, 52) : R.range(36, 44),
                                   v[1] < 0 ? R.range(3.2, 3.8) : R.range(3.6, 4.2),
                                   -toward * R.range(.16, .3),
                                   toward * R.range(.35, .6),
                                   toward * R.range(.05, .15),
                                   toward,
                                   .3 + upOrder[i] * .06 + R.range(0, .03),
                                   1.45 + downOrder[i] * .09 + R.range(0, .05),
                                   R.range(0, TAU),
                                   v[1] < 0};
        }
    }
    static double radius(double time) {
        return RX * ease::outCubic(clamp(time / OPEN)) * (1 - ease::inCubic(clamp((time - SHUT) / .45)));
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void puff(const PuffEmission& e) { puffs.spawn(R, shadow::PuffKind::Soot, e.x, e.y, e.vx, e.vy, e.life, e.size); }
    void splash(double x, double y, int count, double k) {
        for (int i = 0; i < count; ++i)
            drops.add(
                {x + R.range(-2, 2), y - 1, R.range(-70, 70) * k, -R.range(90, 220) * k, R.isChance(.4) ? 2. : 1.});
        ripples.push_back({x, t, .7, 1});
        lights.add(x, y - 7, 30, C[5], .55 * k, .25);
    }
    void update(double dt) override {
        t += dt;
        for (auto& a : arms) {
            if (!a.up && t >= a.t0) {
                a.up = true;
                splash(a.x, a.y, 12, 1);
            }
            if (!a.shut && t >= a.t0 + .36) {
                a.shut         = true;
                const auto pts = armPose(a, t);
                if (pts) {
                    const auto& q = pts->back();
                    lights.add(q.x, q.y, 22, C[5], .7, .2);
                    a.snap = Snap{q.x + std::cos(q.angle) * 4, q.y + std::sin(q.angle) * 4, t};
                    for (int i = 0; i < 6; ++i) {
                        const double angle = R.range(0, TAU), sp = R.range(60, 130);
                        emit({ParticleKind::Spark, a.snap->x, a.snap->y, std::cos(angle) * sp,
                              std::sin(angle) * sp - 20, R.range(.12, .3)});
                    }
                    puff({a.snap->x, a.snap->y, 0, -12, .6, 3.5});
                }
            }
            if (!a.down && t >= a.td + .28) {
                a.down = true;
                splash(a.x, a.y, 8, .7);
            }
        }
        const double r = radius(t);
        if (r > 8 && t < SHUT - .4) {
            for (acc += dt * 8; acc > 1; --acc)
                puff({R.range(-.8, .8) * r, -R.range(1, 4), R.range(-8, 8), -R.range(14, 28), R.range(.6, .85),
                      R.range(3, 6)});
            if (R.isChance(dt * 12))
                emit({ParticleKind::Mote, R.range(-.8, .8) * r, -2, R.range(-8, 8), -R.range(30, 70), R.range(.5, .9),
                      R.pick<uint32_t>(std::array<uint32_t, 3>{C[7], C[6], C[5]}), R.range(.6, 1)});
            if (R.isChance(dt * 6))
                emit({ParticleKind::Vapour, R.range(-.7, .7) * r, -R.range(2, 8), R.range(-6, 6), -R.range(10, 22),
                      R.range(.6, .9), R.isChance(.5) ? C[3] : C[4], R.range(3, 6)});
            if (R.isChance(dt * 3)) ripples.push_back({R.range(-.7, .7) * r, t, .6, .5});
        }
        std::erase_if(ripples, [&](const auto& r0) { return t - r0.t0 > r0.life; });
        std::erase_if(splats, [&](const auto& s) { return t - s.t0 > .6; });
        lights.update(dt);
        const auto floor = [](double) { return 0.; };
        drops.update(dt, floor, [&](double x, double, double, double size) {
            if (std::abs(x) < radius(t) * .95)
                ripples.push_back({x, t, .5, .6});
            else
                splats.push_back({x, t, size});
        });
        puffs.update(dt, R, floor);
        ps.update(dt, R, floor, [](double, double) {});
    }
    void drawBack(Paint& p) const override {
        const double r = radius(t), k = r / RX;
        shadow::floorPool(p, C, r, t, ripples);
        if (k > 0) {
            p.glow(0, -16, 80, C[4], .2 * k);
            p.glow(0, -8, r * 1.7 + 12, C[4], .3 * k);
            p.glow(0, 0, r * 1.15, C[5], .16 * k);
        }
        const shadow::PuffLight light{0, 6, k, 70};
        puffs.draw(p, shadow::PuffKind::Soot, &light);
        ps.draw(p, Paint::Layer::Back);
        for (const auto& a : arms)
            if (a.back) {
                const auto pts = armPose(a, t);
                if (pts) {
                    drawArm(p, a, *pts, DIM);
                    drawHand(p, a, *pts, DIM, t);
                }
            }
    }
    void draw(Paint& p) const override {
        for (const auto& a : arms)
            if (!a.back) {
                const auto pts = armPose(a, t);
                if (pts) {
                    drawArm(p, a, *pts, C);
                    drawHand(p, a, *pts, C, t);
                }
            }
        for (const auto& a : arms)
            if (a.snap && t - a.snap->t0 < .22) {
                const double u = (t - a.snap->t0) / .22;
                shadow::ringFast(p, a.snap->x, a.snap->y, 2 + 5 * ease::outCubic(u), 1,
                                 u < .4   ? C[6]
                                 : u < .7 ? C[5]
                                          : C[3]);
            }
        drops.draw(p, C[1], C[5]);
        for (const auto& s : splats) {
            const double a = 1 - (t - s.t0) / .6;
            p.dot(s.x, -.5, C[1], a * 1.5);
            p.dot(s.x + 1, -.5, C[2], a);
            p.dot(s.x, -1.5, C[5], a);
        }
        ps.draw(p, Paint::Layer::Front);
        for (const auto& l : lights.list) {
            const double q = l.life / l.max;
            p.glow(l.x, l.y, l.radius * (.6 + .4 * q), C[5], l.strength * q * q);
        }
    }
};
}  // namespace
const Descriptor& graspingHandsDescriptor() {
    static const Descriptor d{
        {PixelFxKind::GraspingHands, "shadow/grasping-hands", 200, 116, 100, 100, 3.1, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 5;
            s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                         hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<GraspingHands>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
