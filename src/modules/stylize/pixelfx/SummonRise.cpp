// eve-hot-path
// Native adaptation of pixelfx spawn/summon-rise.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SummonBeast.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double IGN = .5, RUNES = .78, BREAK = 1.05, EYES = 2.3, FADE = 3.4, SY = .3, R0 = 36, R1 = 27, D = 4.6;
const uint32_t   SOOT = hex(0x2a1e2a);
const std::vector<std::vector<std::vector<Point>>> GLYPHS{
    {{{0, -2}, {0, 2}}, {{-1.5, -1}, {1.5, 1}}},
    {{{-1.5, 2}, {0, -2}, {1.5, 2}}, {{-.8, .4}, {.8, .4}}},
    {{{-1.5, -2}, {-1.5, 2}, {1.5, 2}}, {{0, -2}, {1.5, -.5}}},
    {{{0, -2}, {0, 2}}, {{0, -2}, {1.6, -.6}, {0, .6}}},
    {{{-1.5, -2}, {1.5, 2}}, {{1.5, -2}, {-1.5, 2}}, {{-1.5, 0}, {1.5, 0}}},
    {{{-1.4, -2}, {1.4, -2}, {-1.4, 2}, {1.4, 2}}}};
struct SummonRune {
    double angle;
    size_t glyph;
};
struct SummonEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct SummonRock {
    double x, y, vx, vy, bounce, life, angle, angularVelocity, size;
};
struct SummonRise final : Kernel {
    const std::vector<uint32_t>     C, SH, HR, ST;
    const uint32_t                  DUST;
    Rng                             R;
    const std::array<uint32_t, 5>   RUB;
    const summon::Beast&            beast;
    std::array<SummonRune, 12>      runes;
    std::vector<std::vector<Point>> cracks;
    Particles                       ps;
    Bits                            rocks;
    Lights                          lights;
    double                          t       = 0;
    bool                            ignited = false, runesLit = false, broken = false, heaved = false, eyesOpen = false;
    explicit SummonRise(const PixelFxSettings& o)
        : C(o.palette),
          SH(o.spawn.body),
          HR(o.spawn.horn),
          ST(o.spawn.stone),
          DUST(mix(ST[2], hex(0x7a6c78), .45)),
          R(o.seed),
          RUB{ST[0], ST[1], mix(ST[2], C[2], .3), mix(ST[3], C[4], .3), mix(ST[4], C[5], .35)},
          beast(summon::beast()) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        for (size_t i = 0; i < runes.size(); ++i)
            runes[i] = {(double(i) / 12) * TAU + .13, (i * 5 + 2) % GLYPHS.size()};
        for (int i = 0; i < 7; ++i) {
            double             a = (double(i) / 7) * TAU + R.range(-.25, .25), r = 3;
            std::vector<Point> pts{{0, 0}};
            const double       length = R.range(R1 * .6, R1 * 1.05);
            while (r < length) {
                r += R.range(2, 4);
                a += R.range(-.3, .3);
                pts.push_back({std::cos(a) * r, std::sin(a) * r * SY});
            }
            cracks.push_back(std::move(pts));
        }
    }
    double rise(double time) const {
        if (time < BREAK) return 0;
        const double a   = ease::outCubic(clamp((time - BREAK) / .4)) * .56,
                     sag = .04 * std::sin(PI * clamp((time - BREAK - .42) / .3)),
                     b   = ease::outBack(clamp((time - BREAK - .62) / .45), 1.4) * .44;
        return a - sag + b;
    }
    double heat() const {
        return smooth(0, IGN, t) * (1 - smooth(FADE, FADE + .8, t)) * (1 - .25 * smooth(EYES + .2, EYES + .9, t)) *
               (.85 + .15 * noise(t * 9, 3u) + .5 * std::exp(-std::pow(t - BREAK, 2) / .02) +
                .3 * std::exp(-std::pow(t - EYES, 2) / .01));
    }
    double left(double a, double b) { return std::max(.05, std::min(R.range(a, b), D - .1 - t)); }
    void   emit(const SummonEmission& e) {
        ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    void rock(const SummonRock& e) {
        Debris b;
        b.kind            = DebrisKind::Chunk;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = 1;
        b.drag            = .7;
        b.bounce          = e.bounce;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.angularVelocity;
        b.size            = e.size;
        b.ramp.assign(RUB.begin(), RUB.end());
        rocks.add(std::move(b));
    }
    std::vector<Point> topEdge() const {
        const double       off = summon::Beast::HEIGHT * (1 - rise(t));
        std::vector<Point> pts;
        for (int x = 0; x < summon::Beast::WIDTH; ++x)
            for (int y = 0; y < summon::Beast::HEIGHT; ++y)
                if (beast.part[size_t(y * summon::Beast::WIDTH + x)]) {
                    const double ly = y - summon::Beast::HEIGHT + off;
                    if (ly < 0) pts.push_back({x - double(summon::Beast::WIDTH) / 2, ly});
                    break;
                }
        return pts;
    }
    void update(double dt) override {
        t += dt;
        const double h = heat();
        if (t < IGN) {
            const double ph = (t / IGN) * PI;
            for (int side : {-1, 1}) {
                const double a = PI / 2 + side * ph, y = std::sin(a) * R0 * SY;
                if (R.isChance(.5)) {
                    if (y < -.5)
                        emit({ParticleKind::Spark, std::cos(a) * R0, y, R.range(-40, 40), -R.range(40, 130),
                              R.range(.2, .45)});
                    else
                        emit({ParticleKind::Ember, std::cos(a) * R0, y, R.range(-20, 20), -R.range(30, 70),
                              R.range(.3, .6), C[6], 1.4});
                }
            }
        }
        if (t > .2 && t < FADE + .6 && R.isChance(.2 * h)) {
            const double a = R.range(0, TAU), r = R.pick<double>(std::array{R0, R1, R0});
            emit({ParticleKind::Ember, std::cos(a) * r, std::sin(a) * r * SY, R.range(-10, 10), -R.range(20, 50),
                  left(.8, 1.6), C[5], R.range(1, 1.8)});
        }
        if (t > .15 && R.isChance(.4 * std::min(1., h))) {
            const double a = R.range(0, TAU), r = R.isChance(.7) ? R0 : R1;
            if (t < IGN ? std::abs(std::fmod(a - PI / 2 + PI * 3, TAU) - PI) < (t / IGN) * PI : true)
                emit({ParticleKind::Fire, std::cos(a) * r, std::sin(a) * r * SY - .5, R.range(-4, 4), -R.range(6, 16),
                      R.range(.18, .4), 0, R.range(.8, 1.6) * std::min(1., h), 0,
                      R.isChance(.25) ? ParticleFlag::Smokes : ParticleFlag::None});
        }
        if (t > .3 && t < FADE + .4 && R.isChance(.05 * h)) {
            const double a = R.range(0, TAU);
            emit({ParticleKind::Smoke, std::cos(a) * R0 * .9, std::sin(a) * R0 * SY - 1, R.range(-4, 4),
                  -R.range(12, 24), left(1.4, 2.2), 0, R.range(2.2, 3.4)});
        }
        if (!ignited && t >= IGN) {
            ignited = true;
            lights.add(0, -4, 60, C[4], .55, .5);
        }
        if (!runesLit && t >= RUNES) {
            runesLit = true;
            lights.add(0, -4, 50, C[5], .5, .35);
        }
        if (t > RUNES && t < BREAK && R.isChance(.35))
            emit({ParticleKind::Grit, R.range(-R1, R1), -.5, R.range(-20, 20), -R.range(40, 120), R.range(.5, .9),
                  R.pick<uint32_t>(std::array{ST[2], ST[3], ST[4]}), 1});
        if (t > RUNES && t < BREAK && R.isChance(.1))
            emit({ParticleKind::Dust, R.range(-R1, R1), -1, R.range(-12, 12), -R.range(4, 12), R.range(.4, .7), DUST,
                  R.range(1.5, 2.5)});
        if (!broken && t >= BREAK) {
            broken = true;
            lights.add(0, -18, 64, C[4], .9, .6);
            lights.add(0, -8, 26, C[6], .8, .14);
            for (int i = 0; i < 18; ++i)
                rock({R.range(-14, 14), -2, R.range(-75, 75), -R.range(170, 320), .32, R.range(1.6, 2.4),
                      R.range(0, TAU), R.range(-14, 14), R.range(1.3, 3)});
            for (int i = 0; i < 60; ++i) {
                const double u = R.range(-1, 1), a = -PI / 2 + u * .7, sp = R.range(80, 230) * (1 - .4 * std::abs(u));
                emit({ParticleKind::Grit, u * 10, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(1, 1.8),
                      R.pick<uint32_t>(std::array{ST[2], ST[3], ST[3], ST[4]}), R.isChance(.4) ? 2. : 1.});
            }
            for (int i = 0; i < 12; ++i) {
                const double side = R.sign();
                emit({ParticleKind::Smoke, side * R.range(2, 14), -R.range(1, 5), side * R.range(35, 100),
                      -R.range(4, 24), R.range(1.2, 2), DUST, R.range(3.5, 6), R.range(0, .1), ParticleFlag::Big});
            }
            for (int i = 0; i < 4; ++i) {
                const double side = R.sign();
                emit({ParticleKind::Smoke, side * R.range(10, 18), -R.range(2, 8), side * R.range(10, 30),
                      -R.range(40, 90), R.range(1.4, 2.2), DUST, R.range(4, 6.5), R.range(0, .12), ParticleFlag::Big});
            }
            for (int i = 0; i < 24; ++i) {
                const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(150, 360);
                emit({ParticleKind::Spark, R.range(-6, 6), -2, std::cos(a) * sp, std::sin(a) * sp, R.range(.25, .65)});
            }
        }
        const double rv = rise(t) - rise(t - dt);
        if (t > BREAK && t < BREAK + 1.2 && rv > 0) {
            if (R.isChance(std::min(.9, rv * 140))) {
                const auto edge = topEdge();
                if (!edge.empty()) {
                    const auto& e = edge[size_t(std::floor(R.next() * double(edge.size())))];
                    emit({ParticleKind::Grit, e.x, e.y, R.range(-25, 25), -R.range(10, 60), R.range(.8, 1.4),
                          R.pick<uint32_t>(std::array{ST[2], ST[3], ST[4]}), R.isChance(.3) ? 2. : 1.});
                }
            }
            if (R.isChance(std::min(.8, rv * 70))) {
                const double side = R.sign();
                emit({ParticleKind::Smoke, side * R.range(10, 22), -R.range(1, 4), side * R.range(30, 80),
                      -R.range(2, 12), R.range(1, 1.8), DUST, R.range(3, 5.5), 0, ParticleFlag::Big});
            }
        }
        if (!heaved && t >= BREAK + .64) {
            heaved = true;
            lights.add(0, -10, 56, C[4], .6, .4);
            for (int i = 0; i < 8; ++i) {
                const double x = R.signedRange(10, 22), vx = R.signedRange(25, 70);
                rock({x, -2, vx, -R.range(90, 180), .3, R.range(1.2, 1.8), R.range(0, TAU), R.range(-12, 12),
                      R.range(1.2, 2.4)});
            }
        }
        if (!eyesOpen && t >= EYES) {
            eyesOpen = true;
            for (const auto& e : beast.glints) {
                lights.add(e.x, e.y, 14, C[6], .7, .3);
                for (int i = 0; i < 5; ++i)
                    emit({ParticleKind::Spark, e.x, e.y, R.range(-90, 90), R.range(-90, 20), R.range(.15, .35)});
            }
            for (int i = 0; i < 10; ++i) {
                const double side = R.sign();
                emit({ParticleKind::Dust, side * R.range(8, 20), -1, side * R.range(30, 80), -R.range(3, 10),
                      R.range(.4, .8), DUST, R.range(1.5, 3)});
            }
        }
        if (t > FADE + .05 && t < FADE + .75 && R.isChance(.45)) {
            const int x = R.integer(4, summon::Beast::WIDTH - 5), y = R.integer(2, summon::Beast::HEIGHT - 4);
            if (beast.part[size_t(y * summon::Beast::WIDTH + x)]) {
                const double lx = x - double(summon::Beast::WIDTH) / 2, ly = y - summon::Beast::HEIGHT;
                emit({ParticleKind::Smoke, lx, ly, R.range(-6, 6), -R.range(10, 26), left(.9, 1.5), SOOT,
                      R.range(2, 3.4)});
                if (R.isChance(.3))
                    emit({ParticleKind::Ember, lx, ly, R.range(-10, 10), -R.range(15, 40), left(.6, 1.2), C[4], 1});
            }
        }
        rocks.update(dt, [](double) { return 0.; });
        for (auto& b : rocks.list)
            if (b.rest && !b.dusted) {
                b.dusted = true;
                for (int i = 0; i < 2; ++i) {
                    const double x = b.x + R.range(-1.5, 1.5), vx = R.signedRange(6, 30);
                    emit({ParticleKind::Dust, x, -.5, vx, -R.range(2, 12), R.range(.3, .6), DUST, R.range(1.2, 2.4)});
                }
            }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double h = heat();
        if (h <= .01) return;
        p.glow(0, -2, 52, C[4], .28 * h);
        p.glow(0, -22, 72, C[4], .1 * h);
        p.glow(0, -1, 22, C[5], .2 * h);
        const double ck = smooth(RUNES, BREAK, t) * (1 - smooth(FADE, FADE + .7, t));
        if (ck > 0)
            for (const auto& pts : cracks)
                for (size_t i = 1; i < pts.size(); ++i) {
                    const double u = double(i) / double(pts.size());
                    if (u > ck * 1.1) break;
                    const double cool = smooth(BREAK + .1, BREAK + 1.1, t), ci = jsRound(6 - u * 2 - cool * 2.6);
                    p.line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, C[size_t(clamp(ci, 2, 6))], 1);
                }
        constexpr int n    = 220;
        const double  prog = clamp(t / IGN) * PI;
        for (int j = 0; j < n; ++j) {
            const double a = (double(j) / n) * TAU, d = std::abs(std::fmod(a - PI / 2 + PI * 3, TAU) - PI);
            if (d > prog) continue;
            const bool   fresh = prog - d < .25 && t < IGN + .1;
            const double f = noise(a * 6 + t * 5, 4u) * .5 + .5, k = h * (.7 + .5 * f), ca = std::cos(a),
                         sa = std::sin(a);
            p.dot(ca * R0, sa * R0 * SY, fresh ? WHITE : k > .95 ? C[6] : k > .6 ? C[5] : k > .35 ? C[4] : C[3]);
            p.dot(ca * (R0 + 2.2), sa * (R0 + 2.2) * SY, k > .8 ? C[4] : C[3], .9);
            if (t > RUNES - .15)
                p.dot(ca * R1, sa * R1 * SY, k > .9 ? C[5] : k > .5 ? C[4] : C[3], smooth(RUNES - .15, RUNES + .05, t));
            if (j % 11 == 0) p.glow(ca * R0, sa * R0 * SY, 9, C[4], .22 * k);
        }
        const double rk = smooth(RUNES - .1, RUNES + .15, t);
        if (rk > 0)
            for (const auto& r : runes) {
                const double k = h * rk * (.75 + .35 * noise(t * 4 + r.angle * 5, 8u)), mid = (R0 + R1) / 2 + .5;
                const Point  tangent{-std::sin(r.angle), std::cos(r.angle)},
                    normal{std::cos(r.angle), std::sin(r.angle)};
                const auto project = [&](const Point& v) {
                    return Point{normal.x * mid + tangent.x * v.x * 1.1 + normal.x * v.y,
                                 (normal.y * mid + tangent.y * v.x * 1.1 + normal.y * v.y) * SY};
                };
                for (const auto& stroke : GLYPHS[r.glyph])
                    for (size_t i = 1; i < stroke.size(); ++i) {
                        const auto a = project(stroke[i - 1]), b = project(stroke[i]);
                        p.line(a.x, a.y, b.x, b.y, k > .9 ? C[6] : k > .55 ? C[5] : C[4], std::min(1., k * 1.5));
                    }
            }
        const double hole = smooth(BREAK - .05, BREAK + .15, t) * (1 - smooth(BREAK + .95, BREAK + 1.5, t));
        if (hole > 0) {
            const double hw = 15 * hole;
            p.shader(-hw - 1, -1, hw + 1, 4, [&](double x, double y, ShadeOutput& out) {
                const double d = jsHypot2(x / hw, (y - .5) / 3.2);
                if (d > 1) return;
                out.c = d > .86 ? C[4] : d > .62 ? C[1] : C[0];
            });
            p.glow(0, 0, 24, C[5], .3 * hole * h);
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        constexpr int W = summon::Beast::WIDTH, H = summon::Beast::HEIGHT;
        const double  r = rise(t), off = H * (1 - r), h = heat(), fade = smooth(FADE + .1, FADE + .8, t);
        if (r > 0) {
            const double x0 = -double(W) / 2, oy = off - H, k = std::min(1., h);
            const auto   gone = [&](double x, double y) {
                return fade > 0 && fade > .06 + .86 * (.55 * (1 - y / H) + .45 * rand({jsUint(x), jsUint(y), 3u}));
            };
            for (const auto& e : beast.outline)
                if (e.y + oy < 0 && !gone(e.x, std::min(double(H - 1), std::max(0., e.y))))
                    p.px(x0 + e.x, e.y + oy, SH[0]);
            for (int y = 0; y < H; ++y) {
                const double ly = y + oy;
                if (ly >= 0) break;
                const double upRim = std::exp(ly / 17) * k, upPlane = std::exp(ly / 19) * k;
                for (int x = 0; x < W; ++x) {
                    const size_t i    = size_t(y * W + x);
                    const auto   part = beast.part[i];
                    if (!part || gone(x, y)) continue;
                    const auto&  ramp = part == 2 ? HR : SH;
                    const double a    = beast.ambient[i];
                    int          si   = beast.crease[i] ? 0 : a > .74 ? 4 : a > .56 ? 3 : a > .36 ? 2 : 1;
                    if (part == 2 && si > 0) si = std::min(4, si + (y < 3 ? 2 : 1));
                    const double Lr = beast.rim[i] * upRim, Lp = beast.down[i] * upPlane * .6;
                    uint32_t     c = ramp[size_t(si)];
                    if (Lr > .7)
                        c = C[5];
                    else if (Lr > .48)
                        c = C[4];
                    else if (Lr > .3 || Lp > .42)
                        c = C[3];
                    else if (Lr > .17 || Lp > .26)
                        c = mix(C[2], ramp[size_t(si)], .3);
                    else if (Lr > .08 || Lp > .12)
                        c = mix(ramp[size_t(si)], C[1], .6);
                    p.px(x0 + x, ly, c);
                }
            }
            for (const auto& e : beast.tusks)
                if (e.y + oy < 0 && !gone(e.x, e.y)) p.px(x0 + e.x, e.y + oy, mix(HR[4], C[4], .35 * k));
            for (const auto& e : beast.eyes)
                if (e.y + oy < 0 && !gone(e.x, e.y))
                    p.px(x0 + e.x, e.y + oy,
                         t > EYES - .05   ? (t < EYES + .1 ? WHITE : C[6])
                         : t > BREAK + .5 ? C[2]
                                          : SH[0]);
            if (t > EYES - .05) {
                const double eyeK = (1 - smooth(FADE + .6, FADE + 1, t)) * (.85 + .15 * std::sin(t * 13));
                for (const auto& e : beast.glints) {
                    const double ey = e.y + off;
                    if (ey >= 0) continue;
                    p.glow(e.x + .5, ey, 6, C[5], .5 * eyeK);
                    const double g = (t - EYES) / .3;
                    if (g > 0 && g < 1) {
                        const double length = 9 * std::pow(1 - g, 1.5);
                        p.lineFn(
                            e.x - length, ey, e.x + length, ey,
                            [&](double u) {
                                return std::abs(u - .5) < .18 ? WHITE : std::abs(u - .5) < .35 ? C[6] : C[5];
                            },
                            1.);
                        if (g < .4) p.line(e.x, ey - 2 * (1 - g), e.x, ey + 2 * (1 - g), C[6]);
                    } else if (fade > .6 && eyeK > .05)
                        p.px(e.x, ey, C[6]);
                }
            }
            p.glow(0, -6, 30, C[4], .25 * h * r);
        }
        rocks.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& summonRiseDescriptor() {
    static const std::array<Option, 3> options{{{"body", 1, 64, false, 5, &PixelFxSpawnSettings::body},
                                                {"horn", 1, 64, false, 5, &PixelFxSpawnSettings::horn},
                                                {"stone", 1, 64, false, 5, &PixelFxSpawnSettings::stone}}};
    static const Descriptor            d{
        {PixelFxKind::SummonRise, "spawn/summon-rise", 176, 116, 88, 94, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 9;
            s.palette     = {hex(0x1a0608), hex(0x3d0c0c), hex(0x7a1a10), hex(0xc0381a),
                             hex(0xf06a1e), hex(0xffae3a), hex(0xffe08a), WHITE};
            s.spawn.body  = {hex(0x060409), hex(0x0e0913), hex(0x170f1e), hex(0x211628), hex(0x2d2036)};
            s.spawn.horn  = {hex(0x0c080c), hex(0x201820), hex(0x352a32), hex(0x4e3f48), hex(0x6e5c64)};
            s.spawn.stone = {hex(0x1a1424), hex(0x2b2639), hex(0x4a4263), hex(0x6c6488), hex(0x9a91b4)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SummonRise>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
