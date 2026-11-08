// eve-hot-path
// Native adaptation of pixelfx spawn/spawn-portal.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double              OPEN = .3, BURST = 1, SHUT = 1.9, POP = 2.22, SY = .34;
const uint32_t                VOID = hex(0x06030b), FUME = hex(0x3a3046), DUST = hex(0x5a5070);
const std::array<uint32_t, 5> STONE{hex(0x1a1424), hex(0x2b2639), hex(0x4a4263), hex(0x6c6488), hex(0x9a91b4)};
struct Swirl {
    double   angle, radius, speed, inward;
    uint32_t color;
};
struct PortalWisp {
    double            x, y, vx, vy, life, max;
    std::deque<Point> trail;
    double            accumulator, phase;
};
struct PortalRing {
    double born, life, r0, r1, thickness;
    bool   hot;
};
struct PortalEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
    ParticleFlag flags = ParticleFlag::None;
};
struct PortalRock {
    double x, y, vx, vy, life, angle, angularVelocity, size;
};
double lipAt(double th, double spin) {
    return 1 + .05 * noise(th * 3 + spin * .6, 5u) + .035 * noise(th * 8 - spin * 1.3, 9u);
}
struct SpawnPortal final : Kernel {
    const std::vector<uint32_t>   C;
    const double                  RX;
    Rng                           R;
    const std::array<uint32_t, 3> SM;
    Particles                     ps;
    Bits                          rocks;
    Lights                        lights;
    std::vector<Swirl>            swirl;
    std::vector<PortalWisp>       wisps;
    std::vector<PortalRing>       rings;
    bool                          opened = false, belched0 = false, belched1 = false, bursted = false, closed = false;
    double                        t = 0;
    explicit SpawnPortal(const PixelFxSettings& o)
        : C(o.palette),
          RX(o.spawn.width),
          R(o.seed),
          SM{mix(C[4], FUME, .4), mix(C[3], FUME, .3), mix(C[5], FUME, .55)} {
        ps.sparks = {C[7], C[6], C[5], C[3]};
    }
    double radius() const {
        if (t < OPEN) return RX * ease::outBack(clamp(t / OPEN), 2.2);
        if (t < SHUT) return RX * (1 + .035 * std::sin(t * 9) + .1 * std::exp(-std::pow(t - BURST - .05, 2) / .006));
        const double k = clamp((t - SHUT) / (POP - SHUT));
        return RX * (k < .35 ? 1 + .1 * std::sin((k / .35) * PI * .5) : 1.1 * (1 - std::pow((k - .35) / .65, 1.6)));
    }
    double spin() const {
        return t * 2.4 + 1.5 * smooth(BURST - .1, BURST + .3, t) + 10 * std::pow(clamp((t - SHUT) / (POP - SHUT)), 2.2);
    }
    double heat() const { return .85 + .15 * std::sin(t * 7) + .6 * std::exp(-std::pow(t - BURST, 2) / .01); }
    void   emit(const PortalEmission& e) {
        ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay, e.flags);
    }
    void rock(const PortalRock& e) {
        Debris b;
        b.kind            = DebrisKind::Chunk;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = 1;
        b.drag            = .4;
        b.bounce          = .3;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.angularVelocity;
        b.size            = e.size;
        b.ramp.assign(STONE.begin(), STONE.end());
        rocks.add(std::move(b));
    }
    void addSwirl(double r) {
        swirl.push_back({R.range(0, TAU), r, R.range(2.2, 4), R.range(.3, .7),
                         R.pick<uint32_t>(std::array{WHITE, C[6], C[6], C[5]})});
    }
    void pull() {
        const double a = R.range(PI * 1.03, PI * 1.97), distance = R.range(40, 86), life = R.range(.24, .42);
        const double x = std::cos(a) * distance * 1.08, y = std::sin(a) * distance * .85, length = jsHypot2(x, y),
                     v = (.6 * length) / (1 - std::exp(-.6 * life));
        emit({ParticleKind::Mote, x, y, (-x / length) * v, (-y / length) * v, life,
              R.pick<uint32_t>(std::array{WHITE, C[6], C[5], C[5], C[4]}), R.range(1, 1.7)});
    }
    void puff(int count, bool big, double sp) {
        for (int i = 0; i < count; ++i) {
            const double a = R.range(0, TAU), d = R.range(0, .55) * RX;
            emit({ParticleKind::Smoke, std::cos(a) * d, std::sin(a) * d * SY - 2, R.range(-1, 1) * sp * .6,
                  -R.range(.4, 1) * sp, R.range(1.2, 2.2) * (big ? 1.15 : .8), R.pick<uint32_t>(SM),
                  R.range(2.8, 4.6) * (big ? 1.15 : .9), big ? R.range(0, .3) : R.range(0, .1),
                  big ? ParticleFlag::Big : ParticleFlag::None});
        }
    }
    void sparks(int count, double up, double spread) {
        for (int i = 0; i < count; ++i) {
            const double a = -PI / 2 + R.range(-spread, spread), sp = R.range(.5, 1) * up;
            emit(
                {ParticleKind::Spark, R.range(-.5, .5) * RX, -1, std::cos(a) * sp, std::sin(a) * sp, R.range(.35, .8)});
        }
    }
    void update(double dt) override {
        t += dt;
        const bool shutting = t > SHUT && t < POP;
        if ((t < OPEN + .15 || shutting) && R.isChance(.55)) {
            const double side = R.sign(), distance = R.range(RX * 1.3, RX * 2.4), life = R.range(.18, .3),
                         v = (distance * 3) / (std::exp(3 * life) - 1);
            emit({ParticleKind::Streak, side * distance, -R.range(0, 3), -side * v, R.range(-2, 2), life,
                  R.isChance(.5) ? C[6] : C[5]});
        }
        if (t > .1 && t < POP - .1 && R.isChance(shutting ? .85 : t < BURST ? .55 : .1)) pull();
        if (t < POP && t > .12 && R.isChance(shutting ? .5 : .25)) addSwirl(R.range(1.1, 1.5));
        if (!opened && t >= OPEN * .7) {
            opened = true;
            lights.add(0, -8, 50, C[4], .75, .45);
            rings.push_back({t, .42, RX * .8, RX * 1.9, 2.5, true});
            for (int i = 0; i < 8; ++i) {
                const double x = R.signedRange(.7, 1.1) * RX, vx = R.signedRange(10, 45);
                emit({ParticleKind::Dust, x, -1, vx, -R.range(4, 14), R.range(.4, .7), DUST, R.range(1.5, 3)});
            }
        }
        if (!belched0 && t >= .52) {
            belched0 = true;
            puff(6, false, 34);
            sparks(8, 210, .55);
            lights.add(0, -6, 44, C[4], .6, .3);
        }
        if (!belched1 && t >= .76) {
            belched1 = true;
            puff(8, false, 40);
            sparks(12, 240, .6);
            lights.add(0, -6, 48, C[4], .65, .3);
        }
        if (!bursted && t >= BURST) {
            bursted = true;
            lights.add(0, -26, 74, C[4], 1, .75);
            lights.add(0, -10, 40, WHITE, .85, .14);
            rings.push_back({t, .55, RX, RX * 2.5, 3.5, true});
            rings.push_back({t + .06, .45, RX * .8, RX * 1.8, 2, false});
            puff(18, true, 70);
            sparks(34, 400, .8);
            for (int i = 0; i < 5; ++i) {
                const double a = -PI / 2 + (i - 2) * .27 + R.range(-.08, .08), sp = R.range(185, 235);
                wisps.push_back({R.range(-4, 4),
                                 -2,
                                 std::cos(a) * sp,
                                 std::sin(a) * sp,
                                 R.range(.75, 1),
                                 1,
                                 {},
                                 0,
                                 R.range(0, TAU)});
            }
            for (int i = 0; i < 12; ++i)
                rock({R.signedRange(.75, 1.05) * RX, -1, R.range(-100, 100), -R.range(150, 280), R.range(1.3, 1.9),
                      R.range(0, TAU), R.range(-14, 14), R.range(1, 2.4)});
            for (int i = 0; i < 14; ++i)
                emit({ParticleKind::Star, R.range(-.8, .8) * RX, -R.range(4, 34), R.range(-30, 30), -R.range(10, 60),
                      R.range(.5, 1.1), R.pick<uint32_t>(std::array{WHITE, C[6], C[5]}), R.range(.8, 2)});
        }
        if (t > OPEN && t < SHUT && R.isChance(.16)) puff(1, false, 22);
        if (t > OPEN && t < SHUT + .1 && R.isChance(.06)) sparks(1, 160, .4);
        if (!closed && t >= POP) {
            closed = true;
            lights.add(0, -4, 50, C[4], .95, .5);
            lights.add(0, -2, 28, WHITE, .9, .12);
            rings.push_back({t, .35, 2, RX * 1.9, 2.5, true});
            for (int i = 0; i < 18; ++i) {
                const double a = R.range(-PI, 0), sp = R.range(90, 270);
                emit({ParticleKind::Spark, 0, -1, std::cos(a) * sp, std::sin(a) * sp * .8, R.range(.25, .6)});
            }
            for (int i = 0; i < 9; ++i) {
                const double a = R.range(0, TAU), sp = R.range(30, 95);
                emit({ParticleKind::Star, 0, -2, std::cos(a) * sp, std::sin(a) * sp * .5 - 20, R.range(.4, .9),
                      R.pick<uint32_t>(std::array{WHITE, C[6]}), R.range(.8, 1.8)});
            }
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Smoke, R.range(-6, 6), -R.range(1, 4), R.range(-34, 34), -R.range(10, 30),
                      R.range(.8, 1.3), SM[2], R.range(2.5, 4.5)});
            for (int i = 0; i < 14; ++i)
                emit({ParticleKind::Star, R.range(-1, 1) * RX * .9, -R.range(.5, 2), R.range(-4, 4), -R.range(0, 4),
                      R.range(.6, 1.4), R.pick<uint32_t>(std::array{C[6], C[5], C[5]}), R.range(.4, .9),
                      R.range(0, .25)});
        }
        for (size_t left = swirl.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        s = swirl[i];
            const double k = shutting ? 2.4 : 1;
            s.angle += s.speed * k * dt * (1.4 / std::max(.3, s.radius));
            s.radius -= s.inward * k * dt;
            if (s.radius < .25 || t > POP) swirl.erase(swirl.begin() + std::ptrdiff_t(i));
        }
        for (size_t left = wisps.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        w = wisps[i];
            w.trail.push_front({w.x, w.y});
            if (w.trail.size() > 12) w.trail.pop_back();
            w.life -= dt;
            if (w.life <= 0 || (w.vy > 0 && w.y > -2)) {
                for (int j = 0; j < 8; ++j) {
                    const double a = R.range(0, TAU), sp = R.range(30, 120);
                    emit({ParticleKind::Spark, w.x, w.y, std::cos(a) * sp + w.vx * .2, std::sin(a) * sp + w.vy * .2,
                          R.range(.2, .5)});
                }
                emit({ParticleKind::Star, w.x, w.y, 0, -6, .45, WHITE, 2.4});
                lights.add(w.x, w.y, 24, C[5], .65, .25);
                wisps.erase(wisps.begin() + std::ptrdiff_t(i));
                continue;
            }
            w.vy += 360 * dt;
            w.vx += std::sin((w.max - w.life) * 16 + w.phase) * 160 * dt;
            w.vx -= w.vx * 1.1 * dt;
            w.vy -= w.vy * 1.1 * dt;
            w.x += w.vx * dt;
            w.y += w.vy * dt;
            for (w.accumulator += dt; w.accumulator > .03; w.accumulator -= .03)
                emit({ParticleKind::Smoke, w.x, w.y, w.vx * .05, w.vy * .05 - 4, R.range(.6, 1), SM[0],
                      R.range(1.5, 2.4)});
            if (R.isChance(.3))
                emit({ParticleKind::Mote, w.x, w.y, -w.vx * .2 + R.range(-15, 15), -w.vy * .2 + R.range(-15, 15),
                      R.range(.2, .4), C[6], .6});
        }
        for (size_t left = rings.size(); left > 0; --left)
            if (t - rings[left - 1].born > rings[left - 1].life) rings.erase(rings.begin() + std::ptrdiff_t(left - 1));
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
    void hole(Paint& p, double rx, double sp, double h, double wind) const {
        if (rx < 1) return;
        const double ry = rx * SY;
        p.shader(-rx - 2, -ry - 2, rx + 2, ry + 2, [&](double x, double y, ShadeOutput& out) {
            const double u = x / rx, v = y / ry, q = jsHypot2(u, v), th = std::atan2(v, u), l = lipAt(th, sp);
            if (q > l) return;
            const double w = q / l;
            out.c          = v < 0 && w > .72 ? (w > .86 ? C[2] : C[1]) : w > .55 ? C[0] : VOID;
        });
        const int n = std::max(40, int(jsRound(rx * 5)));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j <= n; ++j) {
                const double   q = .14 + .82 * (double(j) / n), th = sp + (i * TAU) / 3 - wind * std::log(q),
                               l = lipAt(th, sp), x = std::cos(th) * q * l * rx, y = std::sin(th) * q * l * ry;
                const uint32_t c = q > .8 ? (h > 1.1 ? WHITE : C[6]) : q > .55 ? C[5] : q > .32 ? C[4] : C[3];
                p.dot(x, y, c, q > .3 ? 1 : q / .3);
                if (q > .4 && j % 2 == 0) {
                    const double t2 = th - .32, x2 = std::cos(t2) * q * .94 * l * rx,
                                 y2 = std::sin(t2) * q * .94 * l * ry;
                    p.dot(x2, y2, q > .7 ? C[4] : C[3], .85);
                }
            }
        p.glow(0, 0, rx * .45, C[3], .35 * h);
    }
    void lip(Paint& p, double rx, double sp, double h, bool near) const {
        if (rx < 1) return;
        const double ry = rx * SY;
        const int    n  = int(std::ceil(rx * 7));
        for (int j = 0; j < n; ++j) {
            const double th = (double(j) / n) * TAU, s = std::sin(th);
            if ((s >= 0) != near) continue;
            const double l = lipAt(th, sp), x = std::cos(th) * rx * l, y = s * ry * l, f = noise(th * 9 + sp * 3, 13u);
            p.dot(std::cos(th) * (rx * l + 1.2), s * (ry * l + 1), C[1]);
            p.dot(x, y, near ? (f > -.3 || h > 1.1 ? WHITE : C[6]) : f > .2 ? C[6] : C[5]);
            if (j % 6 == 0) p.glow(x, y, 7, C[5], (near ? .3 : .2) * std::min(1.4, h));
        }
    }
    void ring(Paint& p, const PortalRing& r, bool near) const {
        const double k = (t - r.born) / r.life;
        if (k < 0 || k >= 1) return;
        band(
            p, 0, 0, r.r0 + (r.r1 - r.r0) * ease::outCubic(k), SY * .62, std::max(1., r.thickness * (1 - k)), 1.15 - k,
            C, [&](double g) { return (std::sin(g) >= 0) == near; }, r.hot && k < .15);
    }
    void drawSwirl(Paint& p, bool near) const {
        const double rx = radius();
        for (const auto& s : swirl)
            for (int j = 0; j < 3; ++j) {
                const double a = s.angle - j * .08 * (1.4 / std::max(.3, s.radius)), x = std::cos(a) * s.radius * rx,
                             y = std::sin(a) * s.radius * rx * SY;
                if ((std::sin(a) >= 0) != near) continue;
                p.dot(x, y - .5, j ? C[5] : s.color, j ? .9 - j * .3 : 1);
                if (!j) p.glow(x, y, 3, C[5], .35);
            }
    }
    void drawBack(Paint& p) const override {
        const double rx = radius(), sp = spin(), h = heat();
        if (t < POP) {
            p.glow(0, -4, rx * 1.8 + 10, C[4], .32 * clamp(rx / RX));
            p.glowLine(0, -2, 0, -44, rx * .7 + 6, C[4], .16 * clamp(rx / RX));
        }
        if (t < OPEN * .6) {
            const double k = t / (OPEN * .6), length = RX * 1.1 * ease::outCubic(k);
            p.lineFn(-length, 0, length, 0, [&](double u) { return std::abs(u - .5) < .3 ? WHITE : C[6]; }, 1.);
            p.glowLine(-length, 0, length, 0, 7, C[5], .6);
        }
        if (t > OPEN * .35 && t < POP) {
            hole(p, rx, sp, h + .4 * smooth(SHUT, POP, t), 2.3 + 2.6 * smooth(SHUT, POP - .05, t));
            lip(p, rx, sp, h, false);
        }
        for (const auto& r : rings) ring(p, r, false);
        drawSwirl(p, false);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double rx = radius(), sp = spin(), h = heat();
        if (t > OPEN * .35 && t < POP) lip(p, rx, sp, h, true);
        for (const auto& r : rings) ring(p, r, true);
        drawSwirl(p, true);
        const double b = t - BURST;
        if (b > 0 && b < .45) {
            const double k = b / .45, top = -84 * ease::outExpo(clamp(b / .14)), bot = -1 - 60 * ease::inQuad(k),
                         w0 = RX * .55 * (1 - k * .8);
            p.shader(-w0 - 1, top, w0 + 1, bot, [&](double x, double y, ShadeOutput& out) {
                const double u    = (y - bot) / (top - bot),
                             half = w0 * std::pow(1 - u, .8) * (.85 + .15 * std::sin(y * .7 + t * 40));
                // Math.max(.5, NaN) is NaN in the source: preserve fringe shader decisions.
                const double denominator = std::isnan(half) ? half : std::max(.5, half), d = std::abs(x) / denominator;
                if (d > 1) return;
                out.c = d < .35 ? WHITE : d < .7 ? C[6] : C[5];
            });
            p.glowLine(0, bot, 0, top, w0 + 12, C[5], .55 * (1 - k));
            if (b < .14) flare(p, 0, -4, 36 * (1 - b / .14), 1 - b / .14, C);
        }
        const double q = t - POP;
        if (q > -.08 && q < .25)
            p.sparkle(0, -1, jsRound((q < 0 ? 4 : 12) * (1 - clamp(q / .25))), WHITE, C[5], .7 * (1 - clamp(q / .25)));
        rocks.draw(p);
        for (const auto& w : wisps) {
            const double k = w.life / w.max;
            for (size_t i = 1; i < w.trail.size(); ++i) {
                const auto&  a = w.trail[i - 1];
                const auto&  z = w.trail[i];
                const double u = double(i) / double(w.trail.size());
                p.line(a.x, a.y, z.x, z.y, u < .25 ? WHITE : u < .55 ? C[6] : C[4], 1.15 - u);
                if (u < .35) p.line(a.x + 1, a.y, z.x + 1, z.y, C[5], 1);
            }
            p.circle(w.x, w.y, 2, C[5]);
            p.circle(w.x, w.y, 1.2, C[6]);
            p.px(w.x, w.y, WHITE);
            p.glow(w.x, w.y, 12, C[5], .5 * k + .2);
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& spawnPortalDescriptor() {
    static const std::array<Option, 1> options{{{"width", 0, 256, false, 0, &PixelFxSpawnSettings::width}}};
    static const Descriptor            d{
        {PixelFxKind::SpawnPortal, "spawn/spawn-portal", 208, 150, 104, 122, 3.8, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 6;
            s.spawn.width = 28;
            s.palette     = {hex(0x12081f), hex(0x2a0f4d), hex(0x481b8c), hex(0x7330d0),
                             hex(0xa35cff), hex(0xcf9bff), hex(0xf1dcff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SpawnPortal>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
