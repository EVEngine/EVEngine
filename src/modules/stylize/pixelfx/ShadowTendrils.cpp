// eve-hot-path
// Native adaptation of pixelfx shadow/shadow-tendrils.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ShadowSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RX = 40, OPEN = .34, SHUT = 2.5, SINK = .5, LX = -.6, LY = -.8;
struct Whip {
    double t, a;
};
struct Tendril {
    double              x, y, length, width, lean, curl, lam, om, phase, amplitude, hook, t0, t1;
    bool                back, up = false, down = false;
    std::optional<Whip> whip;
};
struct SpinePoint {
    double x, y, angle, width, u, s;
};
struct TendrilPose {
    std::array<SpinePoint, 125> points{};
    size_t                      count = 0;
};
TendrilPose spine(const Tendril& T, double t) {
    TendrilPose  result;
    const double e = clamp((t - T.t0) / .2), s1 = clamp((t - T.t1) / SINK);
    if (e <= 0 || s1 >= 1) return result;
    const double lift = .07 * std::sin(PI * clamp(s1 / .3)), down = ease::inOutCubic(clamp((s1 - .12) / .88)),
                 rise = ease::outBack(e, 1.8) * (1 + lift) * (1 - down), hidden = (1 - rise) * T.length,
                 writhe = T.amplitude * (.25 + .75 * smooth(T.t0 + .08, T.t0 + .5, t)),
                 hook   = (.04 + T.hook * ease::outBack(clamp((t - T.t0 - .1) / .32), 2.5)) * (1 + .5 * s1) *
                          (1 + .18 * std::sin(t * 2.3 + T.phase));
    double       lean   = T.lean + .12 * std::sin(t * 1.6 + T.phase);
    if (T.whip) lean += T.whip->a * std::exp(-std::pow((t - T.whip->t) / .14, 2));
    double    th = -PI / 2 + lean, x = T.x - std::cos(th) * hidden, y = T.y - std::sin(th) * hidden;
    const int n = int(std::ceil(T.length / .5));
    EV_ASSERT((n >= 0 && size_t(n + 1) <= result.points.size()));
    for (int i = 0; i <= n; ++i) {
        const double s = i * .5, u = s / T.length;
        result.points[result.count++] = {x, y, th, T.width * std::pow(1 - u, .75) + .45, u, s};
        th += .5 * (writhe * std::sin((s / T.lam) * TAU - T.om * t + T.phase) + T.curl * hook * smooth(.58, 1, u));
        x += std::cos(th) * .5;
        y += std::sin(th) * .5;
    }
    return result;
}
void tentacle(Paint& p, const Tendril& T, const TendrilPose& pose, std::span<const uint32_t> C, double t) {
    const auto pts = std::span(pose.points).first(pose.count);
    for (const auto& q : pts) {
        if (q.y > T.y) continue;
        const double nx = -std::sin(q.angle), ny = std::cos(q.angle), ks = nx * LX + ny * LY >= 0 ? 1 : -1;
        for (double v = -q.width; v <= q.width; v += .5) {
            const double x = q.x + nx * v, y = q.y + ny * v, u = (v / q.width) * ks;
            if (y <= T.y) p.px(x, y, u > .55 ? C[2] : u > -.45 ? C[1] : C[0]);
        }
    }
    const double pulse = (t - T.t0) * 150, slow = std::fmod((t - T.t0) * 55, T.length * 1.8) - 4;
    for (const auto& q : pts) {
        if (q.y > T.y) continue;
        const double nx = -std::sin(q.angle), ny = std::cos(q.angle), f = nx * LX + ny * LY, ks = f >= 0 ? 1 : -1;
        // Source ceil(length/.5) includes one endpoint with u>1 and NaN width.
        // Its width-dependent JS typed-array writes do nothing; never cast NaN to a native cell.
        if (std::isfinite(q.width)) {
            const double lit = ks * q.width, x0 = q.x + nx * lit, y0 = q.y + ny * lit, x1 = q.x - nx * lit,
                         y1 = q.y - ny * lit;
            if (y1 <= T.y) p.px(x1, y1, C[3]);
            if (y0 <= T.y)
                p.px(x0, y0, std::abs(f) > .8 && q.u > .15 ? C[7] : q.u > .85 ? C[6] : std::abs(f) > .3 ? C[6] : C[5]);
            if (q.u > .62 && q.u < .93 && std::fmod(q.s, 3) < .5)
                p.px(q.x + nx * T.curl * q.width * .45, q.y + ny * T.curl * q.width * .45, C[4]);
        }
        const double dp = std::min(std::abs(q.s - pulse), std::abs(q.s - slow) * 1.6);
        if (dp < 2.5) p.px(q.x, q.y, dp < 1 ? C[7] : C[5]);
    }
    const auto& tip = pts.back();
    if (tip.y <= T.y) {
        p.px(tip.x, tip.y, WHITE);
        p.glow(tip.x, tip.y, 6, C[6], .25);
    }
    for (size_t i = 0; i < pts.size(); i += 8)
        if (pts[i].y < T.y) p.glow(pts[i].x, pts[i].y, 9, C[4], .1);
    const auto& pq = pts[size_t(std::min(double(pts.size() - 1), std::max(0., jsRound(pulse * 2))))];
    if (pulse < T.length && pq.y < T.y) p.glow(pq.x, pq.y, 10, C[6], .45);
    const auto base = std::find_if(pts.begin(), pts.end(), [&](const auto& q) { return q.y <= T.y; });
    if (base != pts.end() && std::isfinite(base->width)) {
        const double w = base->width + 1.5;
        p.line(base->x - w, T.y, base->x + w, T.y, C[5]);
        p.px(base->x - w - 1, T.y + .5, C[3]);
        p.px(base->x + w + 1, T.y + .5, C[3]);
    }
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
struct ShadowTendrils final : Kernel {
    const std::vector<uint32_t>     C, DIM;
    Rng                             R;
    Particles                       ps;
    shadow::Puffs                   puffs;
    shadow::Drops                   drops;
    std::array<Tendril, 7>          tents{};
    std::vector<shadow::PoolRipple> ripples;
    std::vector<Splat>              splats;
    Lights                          lights;
    double                          t = 0, acc = 0;
    explicit ShadowTendrils(const PixelFxSettings& o)
        : C(o.palette), DIM{C[0], C[0], C[1], C[1], C[3], C[4], C[5], C[6]}, R(o.seed) {
        ps.sparks   = {C[7], C[6], C[5], C[4]};
        ps.haze     = C[4];
        puffs.flame = {C[7], C[6], C[5], C[4], C[3], C[2], C[1], C[1], C[0]};
        puffs.soot  = {mix(C[3], C[2], .55), mix(C[2], C[1], .5)};
        puffs.cap   = mix(C[4], C[2], .5);
        puffs.rim   = {C[6], C[5], C[4]};
        constexpr std::array<std::array<double, 2>, 7> rows{
            {{-22, -2.5}, {-8, -3}, {7, -3}, {21, -2.5}, {-28, 2.5}, {-1, 3}, {27, 2.5}}};
        constexpr std::array<int, 7> order{3, 0, 5, 2, 4, 1, 6}, last{1, 5, 0, 6, 3, 2, 4};
        for (size_t i = 0; i < tents.size(); ++i) {
            const double x = rows[i][0], y = rows[i][1], sx = x < 0 ? -1 : 1;
            const bool   back  = y < 0;
            auto&        T     = tents[i];
            T.x                = x + R.range(-3, 3);
            T.y                = y;
            T.back             = back;
            T.length           = back ? R.range(46, 62) : R.range(32, 46);
            T.width            = back ? R.range(3.2, 4) : R.range(3.8, 4.6);
            T.lean             = sx * (.06 + std::abs(x) / 120) + R.range(-.06, .06);
            T.curl             = R.isChance(.75) ? -sx : sx;
            T.lam              = R.range(36, 50);
            const double omega = R.range(4.5, 7);
            T.om               = omega * R.sign();
            T.phase            = R.range(0, TAU);
            T.amplitude        = R.range(.06, .085);
            T.hook             = R.range(.15, .32);
            T.t0               = .3 + order[i] * .045 + R.range(0, .03);
            T.t1               = 1.5 + last[i] * .1 + R.range(0, .05);
            if (R.isChance(.45)) {
                const double time = R.range(.9, 1.5), a = R.range(.28, .45);
                T.whip = Whip{time, a * R.sign()};
            }
        }
    }
    static double radius(double time) {
        return RX * ease::outCubic(clamp(time / OPEN)) * (1 - ease::inCubic(clamp((time - SHUT) / .45)));
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void puff(const PuffEmission& e) { puffs.spawn(R, shadow::PuffKind::Soot, e.x, e.y, e.vx, e.vy, e.life, e.size); }
    void splash(double x, double y, int count, double power) {
        for (int i = 0; i < count; ++i)
            drops.add({x + R.range(-2, 2), y - 1, R.range(-70, 70) * power, -R.range(90, 230) * power,
                       R.isChance(.4) ? 2. : 1.});
        ripples.push_back({x, t, .7, 1});
        lights.add(x, y - 7, 30, C[5], .6 * power, .25);
    }
    void update(double dt) override {
        t += dt;
        for (auto& T : tents) {
            if (!T.up && t >= T.t0) {
                T.up = true;
                splash(T.x, T.y, 12, 1);
                for (int i = 0; i < 4; ++i)
                    emit({ParticleKind::Spark, T.x, T.y - 2, R.range(-80, 80), -R.range(120, 260), R.range(.2, .45)});
            }
            if (!T.down && t >= T.t1 + SINK * .8) {
                T.down = true;
                splash(T.x, T.y, 6, .6);
            }
            if (t > T.t0 + .2 && t < T.t1 && R.isChance(dt * 1.6)) {
                const auto pose = spine(T, t);
                if (pose.count) {
                    const auto& q = pose.points[size_t(std::floor(R.range(.3, .95) * double(pose.count)))];
                    if (q.y < T.y)
                        emit({ParticleKind::Star, q.x - std::sin(q.angle) * q.width, q.y + std::cos(q.angle) * q.width,
                              0, -4, R.range(.25, .5), C[6], R.range(1, 2.2)});
                }
            }
        }
        const double r = radius(t);
        if (r > 8 && t < SHUT - .4) {
            for (acc += dt * 9; acc > 1; --acc)
                puff({R.range(-.8, .8) * r, -R.range(1, 4), R.range(-8, 8), -R.range(14, 30), R.range(.6, .85),
                      R.range(3, 6)});
            if (R.isChance(dt * 16))
                emit({ParticleKind::Mote, R.range(-.8, .8) * r, -2, R.range(-8, 8), -R.range(30, 80), R.range(.5, 1),
                      R.pick<uint32_t>(std::array<uint32_t, 4>{C[7], C[6], C[6], C[5]}), R.range(.6, 1.1)});
            if (R.isChance(dt * 5))
                emit({ParticleKind::Ember, R.range(-.8, .8) * r, -3, R.range(-10, 10), -R.range(20, 45), R.range(.6, 1),
                      C[6]});
            if (R.isChance(dt * 3)) ripples.push_back({R.range(-.7, .7) * r, t, .6, .5});
            if (R.isChance(dt * 7))
                emit({ParticleKind::Vapour, R.range(-.7, .7) * r, -R.range(2, 8), R.range(-6, 6), -R.range(10, 22),
                      R.range(.6, .9), R.isChance(.5) ? C[3] : C[4], R.range(3, 6)});
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
            p.glow(0, -16, 86, C[4], .22 * k);
            p.glow(0, -8, r * 1.7 + 12, C[4], .32 * k * (.9 + .1 * std::sin(t * 5)));
            p.glow(0, 0, r * 1.15, C[5], .18 * k);
        }
        const shadow::PuffLight light{0, 6, k, 70};
        puffs.draw(p, shadow::PuffKind::Soot, &light);
        ps.draw(p, Paint::Layer::Back);
        for (const auto& T : tents)
            if (T.back) {
                const auto pose = spine(T, t);
                if (pose.count) tentacle(p, T, pose, DIM, t);
            }
    }
    void draw(Paint& p) const override {
        for (const auto& T : tents)
            if (!T.back) {
                const auto pose = spine(T, t);
                if (pose.count) tentacle(p, T, pose, C, t);
            }
        drops.draw(p, C[1], C[5]);
        for (const auto& s : splats) {
            const double a = 1 - (t - s.t0) / .6;
            p.dot(s.x, -.5, C[1], a * 1.5);
            p.dot(s.x + 1, -.5, C[2], a);
            if (s.size > 1) p.dot(s.x - 1, -.5, C[2], a);
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
const Descriptor& shadowTendrilsDescriptor() {
    static const Descriptor d{
        {PixelFxKind::ShadowTendrils, "shadow/shadow-tendrils", 208, 130, 104, 106, 3.2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 3;
            s.palette = {hex(0x06040b), hex(0x150c26), hex(0x2a1150), hex(0x461a84),
                         hex(0x6f2cc0), hex(0x9d5cec), hex(0xcfa6ff), hex(0xf6eaff)};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ShadowTendrils>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
