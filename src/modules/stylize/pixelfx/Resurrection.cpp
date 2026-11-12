// eve-hot-path
// Native adaptation of pixelfx holy/resurrection.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/ResurrectionWings.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double TOP = -140, POUR = .34, CL0 = .5, CL1 = 1.15, H0 = 1.24, H1 = 1.5, HX = 0, HY = -47, HRX = 9.5,
                 HRY = 2.6;
struct Ray {
    double offset, width, brightness, frequency, phase;
};
struct Mote {
    double q, y, velocity, amplitude, phase, frequency;
};
struct EdgeRow {
    double              y;
    std::vector<double> xs;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct Resurrection final : Kernel {
    const std::vector<uint32_t>            C, ash;
    const PixelFxActor                     actor;
    const holy::ActorShape                 shape;
    const std::array<uint32_t, 6>          wingRamp;
    std::vector<EdgeRow>                   rows;
    std::array<Ray, 6>                     rays;
    std::array<size_t, 6>                  cores;
    std::array<Mote, 34>                   motes;
    Rng                                    rng;
    std::optional<holy::ResurrectionWings> wings;
    Particles                              ps;
    Lights                                 lights;
    holy::Feathers                         loose;
    double                                 t      = 0;
    bool                                   poured = false, flashed = false, formed = false, beaten = false;
    explicit Resurrection(const PixelFxSettings& o)
        : C(o.palette),
          ash(o.holy.ash),
          actor(o.actor),
          shape(holy::prepareShape(actor)),
          wingRamp{C[2], C[3], C[4], C[5], C[6], WHITE},
          rng(o.seed) {
        ps.sparks = {WHITE, C[6], C[5], C[3]};
        ps.haze   = C[4];
        for (auto q : shape.edge) {
            if (rows.empty() || rows.back().y != q.y) rows.push_back({q.y, {}});
            rows.back().xs.push_back(q.x);
        }
        for (size_t i = 0; i < 6; ++i) {
            rays[i]  = {-.75 + double(i) * .3 + rng.range(-.08, .08), rng.range(.1, .2), rng.range(.55, 1),
                        rng.range(1.5, 3.5), rng.range(0, TAU)};
            cores[i] = i;
        }
        std::stable_sort(cores.begin(), cores.end(),
                         [&](size_t a, size_t b) { return rays[a].brightness > rays[b].brightness; });
        for (auto& m : motes)
            m = {rng.range(-1, 1),   rng.range(0, 1),   rng.range(4, 11),
                 rng.range(.6, 2.4), rng.range(0, TAU), rng.range(.6, 1.4)};
        wings.emplace(rng);
    }
    void   emit(const Emission& e) { ps.spawn(rng, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    double climbY() const { return lerp(1, shape.top - 2, ease::inOutSine(seg(t, CL0, CL1))); }
    double shaftK() const { return smooth(0, .12, t) * (1 - smooth(3, 3.75, t)); }
    double halfW(double y) const {
        const double w = lerp(.4, 1, ease::outCubic(seg(t, 0, .6))) * lerp(1, .12, ease::inCubic(seg(t, 2.9, 3.6)));
        return lerp(8, 25, (y - TOP) / -TOP) * w;
    }
    void shaft(Paint& p) const {
        const double k = shaftK();
        if (k <= 0) return;
        std::array<float, 121> profile{};
        for (size_t i = 0; i < 121; ++i) {
            const double q = (double(i) - 60) / 40;
            double       I = 0;
            for (const auto& r : rays) {
                const double d = (q - r.offset) / r.width;
                I += r.brightness * std::exp(-d * d) * (.72 + .28 * std::sin(t * r.frequency + r.phase));
            }
            profile[i] = float((I * .6 + .35 * std::max(0., 1 - q * q)) * smooth(1.25, .7, std::abs(q)));
        }
        const double front = lerp(TOP, 1, ease::inQuad(seg(t, 0, POUR))), w0 = halfW(0) * 1.3;
        p.shade(-w0 - 1, TOP, w0 + 1, std::min(front, .5), [&](double x, double y, int sx, int sy) {
            const double w = halfW(y), q = x / w;
            if (q < -1.5 || q > 1.5) return;
            const size_t i = size_t(jsRound(q * 40) + 60);
            const double I = double(profile[i]) * k * (.75 + .25 * smooth(TOP, 0, y)) * smooth(front, front - 10, y);
            if (I <= .01) return;
            p.blendPx(sx, sy, C[6], I * .24);
            p.add(sx, sy, C[4], I * .3);
        });
        for (size_t i = 0; i < 2; ++i) {
            const auto&  r  = rays[cores[i]];
            const double sh = .75 + .25 * std::sin(t * r.frequency + r.phase);
            for (double y = TOP; y < std::min(front, 0.); ++y) {
                const double pulse = .5 + .5 * std::sin(y * .13 - t * 11 + r.phase);
                p.dot(r.offset * halfW(y), y, pulse > .8 ? WHITE : C[6],
                      k * sh * (.15 + .55 * pulse) * smooth(front, front - 10, y));
            }
        }
        if (t < POUR + .05 && front > TOP + 14) {
            const double fy = std::min(front, 0.);
            p.glow(0, fy, 14, C[5], .7 * k);
            for (int dy = -6; dy <= 0; ++dy) p.dot(0, fy + dy, dy > -3 ? WHITE : C[6], 1 + double(dy) / 7);
        }
        for (const auto& m : motes) {
            const double y = TOP + std::fmod(std::fmod(m.y * -TOP + t * m.velocity, -TOP) + -TOP, -TOP);
            if (y > front) continue;
            const double w = halfW(y), x = m.q * w + std::sin(t * m.frequency + m.phase) * m.amplitude;
            const int    i = int(jsRound((x / w) * 40)) + 60;
            if (i < 0 || i >= 121) {
                // Source undefined profile reads produce NaN: dot emits nothing; additive channels coerce to zero.
                const int sx = p.cellX(x, y), sy = p.cellY(x, y);
                if (p.contains(sx, sy) && p.isInsideClip(sx, sy)) p.lights[p.index(sx, sy)] = 0xff000000u;
                continue;
            }
            const double I = double(profile[size_t(i)]) * k;
            if (I < .25) continue;
            p.dot(x, y, I > .75 ? WHITE : I > .5 ? C[6] : C[5], std::min(1., I * 1.4));
            p.light(x, y, C[5], I * .5);
        }
    }
    void pool(Paint& p, bool back) const {
        const double k = shaftK() * smooth(POUR - .05, POUR + .1, t), w = halfW(0) * 1.25;
        if (k <= 0) return;
        if (back) {
            p.glow(0, -2, w * 1.6, C[4], .3 * k);
            return;
        }
        for (int y = 0; y <= 3; ++y)
            for (double x = std::floor(-w); x <= w; ++x) {
                const double u = jsHypot2(x / w, double(y) / 4);
                if (u >= 1) continue;
                const double a = k * (y == 0 ? std::sqrt(1 - u) * 1.2 : (1 - u) * .9);
                p.dot(x, y, u < .3 ? (y ? C[5] : WHITE) : u < .6 ? C[5] : u < .85 ? C[4] : C[3], a);
            }
    }
    void halo(Paint& p) const {
        const double form = ease::outCubic(seg(t, H0, H1)), k = 1 - smooth(3.35, 3.9, t);
        if (form <= 0 || k <= 0) return;
        const bool   white = t >= H1 && t < H1 + .06;
        const double gl    = (t - H1) * 4.2 + PI / 2;
        for (int j = 0; j < 96; ++j) {
            const double g    = (double(j) / 96) * TAU,
                         from = std::abs(std::fmod(std::fmod(g - PI / 2 + PI, TAU) + TAU, TAU) - PI);
            if (from > form * PI + .02) continue;
            const double x = HX + std::cos(g) * HRX * (.9 + .1 * form), y = HY + std::sin(g) * HRY,
                         dg  = std::abs(std::fmod(std::fmod(g - gl + PI, TAU) + TAU, TAU) - PI);
            const bool   hot = white || dg < .22 || from > form * PI - .3;
            if (std::sin(g) >= 0) {
                p.dot(x, y, hot ? WHITE : C[6], k);
                p.dot(x, y + 1, hot ? C[6] : C[4], k);
            } else
                p.dot(x, y, hot ? C[6] : C[5], k * .9);
        }
        p.glow(HX, HY, 20, C[4], .4 * k * form);
        p.glow(HX, HY + 1, 9, WHITE, .2 * k * form);
        const double gx = HX + std::cos(gl) * HRX, gy = HY + std::sin(gl) * HRY;
        if (t > H1 && std::sin(gl) > .2) p.sparkle(gx, gy + .5, 2 + 2 * std::sin(gl), C[6], C[4], .35 * k);
        if (t >= H1 && t < H1 + .3) p.sparkle(HX, HY + HRY + .5, 7 * (1 - (t - H1) / .3), WHITE, C[5], .6);
    }
    void body(Paint& p) const {
        if (t >= CL1 + .6) return;
        const double Ly = climbY();
        if (t < CL1) {
            holy::recolor(
                p, actor,
                [&](uint32_t c, double, double y) -> uint32_t {
                    const double d = y - Ly;
                    if (d < -.5)
                        return ash[size_t(
                            clamp(std::floor(luma(c) * 1.1 * double(ash.size())), 0, double(ash.size() - 1)))];
                    if (d < .5) return WHITE;
                    if (d < 1.5) return C[6];
                    const double k = std::exp(-(d - 1.5) / 4) * .7;
                    return k > .08 ? mix(c, C[5], k) : 0;
                },
                1.);
            for (auto q : shape.out) {
                const double d = q.y - Ly;
                if (d < -.5 || d > 12) continue;
                p.dot(q.x, q.y, d < 1 ? WHITE : d < 5 ? C[5] : C[4], d < 1 ? 1 : 1.1 - d / 12);
            }
            if (t > CL0) {
                p.glow(0, Ly, 26, C[4], .5);
                p.glow(0, Ly, 8, WHITE, .3);
                p.glow(0, -18, 36, C[5], .16 * seg(t, CL0, CL1));
            }
            return;
        }
        const double u = t - CL1;
        holy::recolor(
            p, actor,
            [&](uint32_t c, double, double) -> uint32_t {
                return u < .06 ? WHITE : u < .45 ? mix(c, C[5], .55 * std::pow(1 - u / .45, 2)) : 0;
            },
            1.);
        if (u < .25)
            for (auto q : shape.out) p.dot(q.x, q.y, u < .08 ? WHITE : C[5], 1 - u / .25);
    }
    holy::WingPose                   sample(int side, double time) const { return wings->sample(time, side); }
    std::optional<holy::WingFeather> pick(const holy::WingPose& pose, int kind = -1) {
        std::array<size_t, 18> eligible{};
        size_t                 n = 0;
        for (size_t i = 0; i < pose.count; ++i)
            if (kind < 0 || wings->quills[pose.feathers[i].index].kind == kind) eligible[n++] = i;
        const size_t i = size_t(std::floor(rng.next() * double(n)));
        return n ? std::optional<holy::WingFeather>(pose.feathers[eligible[i]]) : std::nullopt;
    }
    void addLoose(holy::FeatherLaunch launch, int side) {
        launch.side = side;
        loose.add(rng, launch);
    }
    void update(double dt) override {
        t += dt;
        if (t > .05 && t < 2.9 && rng.isChance(.16)) {
            const double q = rng.range(-.9, .9), y = TOP + rng.range(8, 24), v0 = rng.range(50, 90),
                         life = std::log(1 + (3 * (-y - 2)) / v0) / 3;
            emit({ParticleKind::Streak, q * halfW(y), y, q * 2, v0, life, rng.isChance(.5) ? C[6] : C[5]});
        }
        if (t > .3 && t < 2.8 && rng.isChance(.05))
            emit({ParticleKind::Star, rng.range(-1, 1) * halfW(-30), -rng.range(4, 90), rng.range(-3, 3),
                  rng.range(-4, 6), rng.range(.4, .8), rng.isChance(.5) ? C[6] : WHITE, rng.range(.6, 1.8)});
        if (!poured && t >= POUR) {
            poured = true;
            lights.add(0, -12, 42, C[5], .9, .4);
            lights.add(0, -6, 16, WHITE, .6, .1);
            for (int i = 0; i < 14; ++i)
                emit({ParticleKind::Star, rng.range(-18, 18), -rng.range(0, 6), rng.range(-20, 20), -rng.range(10, 50),
                      rng.range(.5, 1.1), rng.isChance(.5) ? WHITE : C[6], rng.range(.6, 2.2)});
            for (int i = 0; i < 10; ++i) {
                const double a = -PI / 2 + rng.range(-1.2, 1.2), sp = rng.range(60, 150);
                emit({ParticleKind::Spark, rng.range(-3, 3), -1, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.25, .5)});
            }
        }
        if (t > CL0 && t < CL1) {
            const double Ly = jsRound(climbY());
            for (double y : {Ly, Ly + 1}) {
                const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.y == y; });
                if (row == rows.end() || !rng.isChance(.35)) continue;
                const double x = rng.pick<double>(row->xs), s = x + .5 > 0 ? 1 : x + .5 < 0 ? -1 : 0;
                emit({rng.isChance(.6) ? ParticleKind::Mote : ParticleKind::Star, x + s, y, s * rng.range(8, 30),
                      -rng.range(15, 45), rng.range(.35, .7), rng.isChance(.5) ? C[6] : C[5], rng.range(.5, 1.1)});
            }
        }
        if (!flashed && t >= CL1) {
            flashed = true;
            lights.add(0, -24, 64, C[5], .7, .5);
            lights.add(0, -30, 28, WHITE, .7, .1);
            for (int i = 0; i < 18; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(30, 110);
                emit({ParticleKind::Star, std::cos(a) * 6, -24 + std::sin(a) * 12, std::cos(a) * sp,
                      std::sin(a) * sp * .7 - 20, rng.range(.4, .9), rng.isChance(.5) ? WHITE : C[6],
                      rng.range(.6, 2)});
            }
        }
        if (t > H0 - .2 && t < H0 + .1 && rng.isChance(.6)) {
            const double g = rng.range(0, TAU), D = rng.range(14, 24), x = HX + std::cos(g) * D,
                         y = HY + std::sin(g) * D * .5, tx = HX + std::cos(g + .6) * HRX,
                         ty = HY + std::sin(g + .6) * HRY, T = rng.range(.16, .24), raw = jsHypot2(tx - x, ty - y),
                         l = raw ? raw : 1, v = (l * 3) / (std::exp(3 * T) - 1);
            emit({ParticleKind::Streak, x, y, ((tx - x) / l) * v, ((ty - y) / l) * v, T,
                  rng.isChance(.5) ? C[6] : C[5]});
        }
        if (!formed && t >= H1) {
            formed = true;
            lights.add(HX, HY, 30, C[6], .8, .3);
        }
        if (t > holy::WING_OPEN && t < holy::WING_OPEN + .35 && rng.isChance(.5))
            for (int side : {-1, 1}) {
                const auto pose = sample(side, t);
                if (const auto f = pick(pose))
                    emit({ParticleKind::Star, f->x + std::cos(f->angle) * f->length,
                          f->y + std::sin(f->angle) * f->length, side * rng.range(5, 25), -rng.range(5, 25),
                          rng.range(.3, .6), rng.isChance(.5) ? WHITE : C[6], rng.range(.6, 1.6)});
            }
        if (t > holy::WING_OPEN + .45 && t < holy::SCATTER)
            for (int side : {-1, 1}) {
                if (rng.isChance(.045)) {
                    const auto pose = sample(side, t);
                    if (const auto f = pick(pose, 0))
                        emit({ParticleKind::Star, f->x + std::cos(f->angle) * (f->length - 2),
                              f->y + std::sin(f->angle) * (f->length - 2), 0, -rng.range(2, 8), rng.range(.3, .6),
                              rng.isChance(.5) ? WHITE : C[6], rng.range(1, 2.4)});
                }
                if (rng.isChance(.03)) {
                    const auto pose = sample(side, t);
                    if (const auto f = pick(pose))
                        emit({ParticleKind::Ember, f->x + std::cos(f->angle) * f->length * .7,
                              f->y + std::sin(f->angle) * f->length * .7, rng.range(-8, 8), -rng.range(8, 25),
                              rng.range(.6, 1.1), C[5]});
                }
            }
        if (!beaten && t >= holy::BEAT2 - .03) {
            beaten = true;
            for (int side : {-1, 1}) {
                lights.add(side * 30, -34, 46, C[5], .6, .4);
                for (int i = 0; i < 7; ++i)
                    emit({ParticleKind::Mote, side * rng.range(8, 22), -rng.range(1, 4), side * rng.range(90, 160),
                          -rng.range(0, 8), rng.range(.25, .45), rng.isChance(.5) ? C[6] : C[5], rng.range(.6, 1)});
                const auto pose = sample(side, t);
                for (size_t i = 0; i < pose.count; ++i) {
                    const auto& f = pose.feathers[i];
                    if (wings->quills[f.index].kind == 0)
                        emit({ParticleKind::Spark, f.x + std::cos(f.angle) * f.length,
                              f.y + std::sin(f.angle) * f.length, side * rng.range(30, 90), rng.range(60, 160),
                              rng.range(.3, .6)});
                }
                for (int i = 0; i < 3; ++i) {
                    const auto w = sample(side, t);
                    if (const auto f = pick(w, 1))
                        addLoose(
                            {f->x + std::cos(f->angle) * f->length * .6, f->y + std::sin(f->angle) * f->length * .6,
                             side * rng.range(20, 70), rng.range(10, 50), f->angle, rng.range(9, 12),
                             rng.range(2.2, 2.8), rng.range(1.2, 1.6)},
                            side);
                }
            }
        }
        for (int side : {-1, 1})
            for (size_t i = 0; i < 18; ++i) {
                auto&        q  = wings->quills[i];
                const size_t si = side > 0 ? 0 : 1;
                if (q.gone[si] || t < q.off[si]) continue;
                q.gone[si]      = true;
                const auto pose = sample(side, q.off[si] - .001);
                const auto f    = pose.find(i);
                if (!f) continue;
                const double ca = std::cos(f->angle), sa = std::sin(f->angle);
                if (q.kind != 2) {
                    const double x = f->x + ca * f->length * .55, y = f->y + sa * f->length * .55, ox = x - side * 4,
                                 oy = y + 20, raw = jsHypot2(ox, oy), l = raw ? raw : 1, sp = rng.range(60, 150);
                    holy::FeatherLaunch launch{x,
                                               y,
                                               (ox / l) * sp + rng.range(-15, 15),
                                               (oy / l) * sp * .5 - rng.range(20, 50),
                                               f->angle + rng.range(-.4, .4),
                                               rng.range(9, 12),
                                               rng.range(2.2, 2.8),
                                               rng.range(1, 1.45)};
                    launch.back = rng.isChance(.35);
                    addLoose(launch, side);
                }
                for (int j = 0; j < 2; ++j)
                    emit({ParticleKind::Ember, f->x + ca * f->length * rng.range(.3, 1),
                          f->y + sa * f->length * rng.range(.3, 1), rng.range(-15, 15), -rng.range(10, 40),
                          rng.range(.6, 1.1), C[5]});
                if (rng.isChance(.6))
                    emit({ParticleKind::Star, f->x + ca * f->length, f->y + sa * f->length, 0, -rng.range(5, 15),
                          rng.range(.3, .6), WHITE, rng.range(1, 2)});
            }
        if (t > 3.35 && t < 3.75 && rng.isChance(.4)) {
            const double g = rng.range(0, TAU);
            emit({ParticleKind::Ember, HX + std::cos(g) * HRX, HY + std::sin(g) * HRY, rng.range(-6, 6),
                  -rng.range(10, 25), rng.range(.5, .9), C[5]});
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
        loose.update(dt);
    }
    void ring(Paint& p, bool back) const {
        if (t > POUR && t < POUR + .4) {
            const double u = seg(t, POUR, POUR + .4);
            band(
                p, 0, 0, 4 + 32 * ease::outCubic(u), .18, 2.5 * (1 - u) + .5, 1.1 - u, std::span(C).subspan(3),
                [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; }, t < POUR + .06);
        }
    }
    void drawBack(Paint& p) const override {
        shaft(p);
        pool(p, true);
        ring(p, true);
        for (int side : {-1, 1}) wings->draw(p, t, side, C);
        loose.draw(p, wingRamp, Paint::Layer::Back);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        pool(p, false);
        ring(p, false);
        body(p);
        if (t > holy::BEAT2 - .03 && t < holy::BEAT2 + .45) {
            const double u = seg(t, holy::BEAT2 - .03, holy::BEAT2 + .45), fx = 10 + 70 * ease::outCubic(u);
            for (int s : {-1, 1})
                for (int d = 0; d < 12; ++d)
                    p.dot(s * (fx - d), 0, d < 2 ? WHITE : d < 5 ? C[6] : C[4], (1 - u) * (1 - double(d) / 12) * 1.3);
        }
        const double wk =
            smooth(holy::WING_OPEN, holy::WING_OPEN + .2, t) * (1 - smooth(holy::SCATTER, holy::SCATTER + .3, t));
        if (wk > 0) {
            for (auto q : shape.out)
                if (q.y < -8) p.light(q.x, q.y, C[5], .55 * wk);
            p.glow(0, -34, 76, C[4], .14 * wk);
        }
        halo(p);
        if (t >= CL1 && t < CL1 + .22)
            flare(p, 0, shape.top - 1, 22 * (1 - (t - CL1) / .22), 1 - (t - CL1) / .22, std::span(C).subspan(3));
        loose.draw(p, wingRamp, Paint::Layer::Front);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& resurrectionDescriptor() {
    static const std::array<Option, 2> options{
        {{"actor", 0, 0, false, 0, &PixelFxSettings::actor}, {"ash", 1, 64, false, 6, &PixelFxHolySettings::ash}}};
    static const Descriptor d{
        {PixelFxKind::Resurrection, "holy/resurrection", 192, 160, 96, 140, 4.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 5;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                              hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
            s.holy.ash     = {hex(0x16131c), hex(0x26222f), hex(0x3a3546), hex(0x524c5e), hex(0x6e6878), hex(0x8c8796)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Resurrection>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
