// eve-hot-path
// Native adaptation of pixelfx arcane/magic-circle-cast.js, MIT.
// Sprite Fusion holy light, Copyright (c) 2026 Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Arcane.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                R = 40, R2 = 35, RI = 24, RR = 30, SY = .3, TOP = 192, W = 11;
constexpr int                   NRUNE = 8;
constexpr std::array<double, 2> PEN{.04, .42}, PEN2{.1, .48}, INNER{.3, .6}, STAR{.52, .88}, RUNES{.9, 1.3};
constexpr double                FLASH = 1.52, BREAK = 2.42, GONE = 3.45, END = 3.9;
double                          outerAt(double g, const std::array<double, 2>& w) {
    return w[0] + (std::abs(std::atan2(std::sin(g - PI / 2), std::cos(g - PI / 2))) / PI) * (w[1] - w[0]);
}
double innerAt(double g) { return INNER[0] + (wrap(g + PI / 2, TAU) / TAU) * (INNER[1] - INNER[0]); }
double runeAngle(int k) { return -PI / 2 + ((k + .5) * TAU) / NRUNE; }
double runeLit(int k) { return RUNES[0] + (k * (RUNES[1] - RUNES[0])) / (NRUNE - 1); }
double runeOut(int k) { return BREAK + .2 + k * .08; }
double level(double t) {
    if (t < RUNES[0]) return .55;
    if (t < FLASH) return .55 + .4 * smooth(RUNES[0], FLASH, t);
    if (t < FLASH + .07) return 1.2;
    if (t < BREAK) return .92;
    return .92 * (1 - smooth(BREAK, GONE, t));
}
struct Strand {
    double offset, width, frequency;
};
struct Edge {
    Point  p0, p1;
    double start, duration;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct MagicCircleCast final : Kernel {
    const std::vector<uint32_t>   C;
    const uint32_t                S;
    Rng                           Rg;
    Particles                     ps;
    Lights                        lights;
    const std::array<uint32_t, 5> col;
    std::array<Strand, 9>         strands{};
    std::array<Edge, 6>           edges{};
    double                        t       = 0;
    bool                          flashed = false, broke = false;
    std::array<bool, NRUNE>       out{};
    explicit MagicCircleCast(const PixelFxSettings& o)
        : C(o.palette), S(o.seed), Rg(S), col{WHITE, C[6], C[5], C[4], C[3]} {
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        for (auto& s : strands) s = {Rg.range(-.85, .85), Rg.range(.04, .1), Rg.range(1, 3)};
        const auto tri = [](double a0) {
            std::array<Point, 3> p{};
            for (int k = 0; k < 3; ++k) {
                const double a = a0 + (k * TAU) / 3;
                p[size_t(k)]   = {std::cos(a) * RI, std::sin(a) * RI};
            }
            return p;
        };
        const auto A = tri(-PI / 2), B = tri(PI / 2);
        for (int k = 0; k < 3; ++k)
            for (int q = 0; q < 2; ++q) {
                const auto& v            = q == 0 ? A : B;
                edges[size_t(k * 2 + q)] = {v[size_t(k)], v[size_t((k + 1) % 3)],
                                            STAR[0] + (k * (STAR[1] - STAR[0])) / 3, (STAR[1] - STAR[0]) / 3};
            }
    }
    void     emit(const Emission& e) { ps.spawn(Rg, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    uint32_t lineCol(double lvl, double age) const {
        return age < .05   ? WHITE
               : age < .16 ? C[6]
               : lvl > 1   ? WHITE
               : lvl > .8  ? C[6]
               : lvl > .45 ? C[5]
               : lvl > .28 ? C[4]
                           : C[3];
    }
    static double lineAlpha(double lvl) { return clamp(lvl / .3); }
    void          planeLine(Paint& p, const Edge& edge, double lvl, bool far) const {
        const auto [x0, v0] = edge.p0;
        const auto [x1, v1] = edge.p1;
        const double s      = clamp((t - edge.start) / edge.duration);
        if (s <= 0) return;
        double u0 = 0, u1 = s;
        if ((v0 < 0) != (v1 < 0)) {
            const double uc = v0 / (v0 - v1);
            if ((v0 < 0) == far)
                u1 = std::min(u1, uc);
            else
                u0 = uc;
        } else if ((v0 < 0) != far)
            return;
        if (u1 <= u0) return;
        const auto point    = [&](double u) { return Point{x0 + (x1 - x0) * u, (v0 + (v1 - v0) * u) * SY}; };
        const auto [xa, ya] = point(u0);
        const auto [xb, yb] = point(u1);
        p.lineFn(
            xa, ya, xb, yb,
            [&](double q) { return lineCol(lvl, t - (edge.start + (u0 + (u1 - u0) * q) * edge.duration)); },
            lineAlpha(lvl));
    }
    void circle(Paint& p, bool far) const {
        if (t >= GONE) return;
        const double lvl = level(t), a = lineAlpha(lvl);
        const bool   chasing = t > RUNES[0] && t < BREAK;
        const double run     = chasing ? (t - RUNES[0]) * 5 + std::pow(t - RUNES[0], 2) * 4 : 0;
        const auto   keep    = [&](double g) { return (std::sin(g) < 0) == far; };
        const auto   ring    = [&](double r, auto when, bool chase) {
            arcane::floorRing(
                p, 0, 0, r, SY,
                [&](double g) {
                    bool hot = false;
                    if (chase)
                        for (double offset : {0., PI})
                            if (std::abs(std::atan2(std::sin(g - run - offset), std::cos(g - run - offset))) < .22)
                                hot = true;
                    return hot ? WHITE : lineCol(lvl, t - when(g));
                },
                [&](double g) { return t < when(g) ? 0 : a; }, keep);
        };
        ring(R, [](double g) { return outerAt(g, PEN); }, chasing);
        ring(R2, [](double g) { return outerAt(g, PEN2); }, false);
        ring(RI, [](double g) { return innerAt(g); }, false);
        for (int j = 0; j < 32; ++j) {
            const double g = ((j + .5) * TAU) / 32;
            if ((std::sin(g) < 0) != far) continue;
            const double when = outerAt(g, PEN2) + .03;
            if (t < when) continue;
            p.line(std::cos(g) * R2, std::sin(g) * R2 * SY, std::cos(g) * R, std::sin(g) * R * SY,
                   lineCol(lvl, t - when), a * (j % 2 ? .6 : 1));
        }
        for (const auto& e : edges) planeLine(p, e, lvl, far);
    }
    void runes(Paint& p, bool far) const {
        for (int k = 0; k < NRUNE; ++k) {
            const double g = runeAngle(k);
            if ((std::sin(g) < 0) != far) continue;
            const double x = std::cos(g) * RR, y0 = std::sin(g) * RR * SY, lit = t - runeLit(k),
                         gone = clamp((t - runeOut(k)) / .25);
            if (t < innerAt(g) || gone >= 1) continue;
            if (lit < 0) {
                arcane::glyph(p, k, x, y0 - 1, C[3]);
                continue;
            }
            const double rise = 3 * ease::outBack(clamp(lit / .2), 2.4), bob = lit > .2 ? std::sin(t * 5 + k) * .5 : 0,
                         y   = y0 - 1 - rise + bob;
            const bool   hot = lit < .07 || (t > FLASH && t < FLASH + .07);
            const double lvl = level(t);
            arcane::glyph(p, k, x, y, hot ? WHITE : lvl > .8 ? WHITE : C[6], 1 - gone, hot ? C[6] : C[4]);
            if (lit < .1) p.sparkle(x, y - 2, 4 * (1 - lit / .1) + 1, C[6], C[5], .5);
            p.glow(x, y - 2, 8, C[5], (.3 + .3 * lvl) * (1 - gone));
            p.glow(x, y0, 6, C[4], .2 * (1 - gone));
        }
    }
    void column(Paint& p) const {
        if (t < FLASH - .22 || t > BREAK + .6) return;
        if (t < FLASH) {
            const double a = smooth(FLASH - .22, FLASH - .04, t), top = -(TOP - 10) * ease::outCubic(a);
            for (double y = -1; y >= top; --y) p.dot(0, y, y > top + 6 ? C[6] : WHITE, a * (.35 + .65 * (1 + y / TOP)));
            p.glow(0, top, 9, C[6], .5 * a);
            p.glow(0, -6, 24, C[5], .4 * a);
            return;
        }
        const double u = t - FLASH, brk = clamp((t - BREAK) / .45), top = -TOP * ease::outExpo(clamp(u / .09)),
                     w     = t < BREAK ? W * (1 + .38 * std::exp(-u * 9)) : W * (1 - ease::outCubic(brk) * .85),
                     fade  = 1 - smooth(BREAK + .25, BREAK + .55, t);
        const bool   white = u < .067;
        if (fade <= 0) return;
        for (int y = 3; y >= int(std::ceil(top)); --y) {
            const int    h     = -y;
            const double ww    = w * (1 + .5 * std::exp(-double(std::max(0, h)) / 6));
            const int    pulse = fract(h * .022 - u * 2.6) < .07 ? 1 : 0;
            for (int x = int(std::floor(-ww * 1.3)); x <= int(std::ceil(ww * 1.3)); ++x) {
                const double q = std::abs(x + .5) / ww;
                if (brk > 0 && noise2(x * .32, h * .11 - (t - BREAK) * 6, S) * .5 + .5 < brk * 1.25 - .1) continue;
                int lv = q < .2 ? 0 : q < .42 ? 1 : q < .66 ? 2 : q < .88 ? 3 : 4;
                for (const auto& s : strands)
                    if (std::abs((x + .5) / ww - s.offset - .05 * std::sin(t * s.frequency + s.offset * 9)) < s.width)
                        lv = std::max(0, lv - 1);
                lv             = white ? 0 : std::max(0, lv - pulse);
                const double a = (q < .88 ? 1 : (1.3 - q) / .42) * fade;
                if (a <= arcane::bayer(x, y) * (q < .66 ? .2 : 1)) continue;
                p.px(x, y, col[size_t(lv)]);
            }
            if ((h & 7) == 0 && h < TOP - ww * 2.4 - 8) p.glow(0, y, ww * 2.4 + 6, C[5], .3 * fade);
        }
        for (int i = 0; i < 6; ++i) {
            const double k = (t - (FLASH + .08 + i * .16)) / .8;
            if (k < 0 || k > 1) continue;
            const double rx = w * 1.4 * (1 + k * .35), ry = rx * .3, yc = -3 - ease::outQuad(k) * 120;
            const int    n = int(std::ceil(rx * 7));
            for (int j = 0; j < n; ++j) {
                const double g = (double(j) / n) * TAU;
                p.dot(std::cos(g) * rx, yc + std::sin(g) * ry, k < .3 ? WHITE : C[6],
                      (1 - k) * fade * (std::sin(g) < 0 ? .6 : 1));
            }
        }
        p.glow(0, -4, 34, C[6], .45 * fade);
        p.glow(0, -60, 70, C[4], .25 * fade);
    }
    void shock(Paint& p, bool far) const {
        for (const auto& v :
             std::array<std::array<double, 5>, 2>{{{0, R * .6, R * 2, 6, .5}, {.08, R * .4, R * 1.4, 3, .4}}}) {
            const auto [d, r0, r1, th, duration] = v;
            const double k                       = (t - FLASH - d) / duration;
            if (k < 0 || k >= 1) continue;
            const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
            band(
                p, 0, 0, r0 + (r1 - r0) * ease::outCubic(k), SY, std::max(1., th * (1 - k)), 1.15 - k, ramp,
                [&](double g) { return (std::sin(g) < 0) == far; }, k < .2);
        }
    }
    void update(double dt) override {
        t += dt;
        if (t > PEN[0] && t < STAR[1] && Rg.isChance(.5)) {
            double x = 0, y = 0;
            if (t < PEN[1]) {
                const double g = PI / 2 + Rg.sign() * ((t - PEN[0]) / (PEN[1] - PEN[0])) * PI;
                x              = std::cos(g) * R;
                y              = std::sin(g) * R * SY;
            } else if (t < INNER[1]) {
                const double g = -PI / 2 + ((t - INNER[0]) / (INNER[1] - INNER[0])) * TAU;
                x              = std::cos(g) * RI;
                y              = std::sin(g) * RI * SY;
            } else {
                std::array<size_t, 6> candidates{};
                size_t                count = 0;
                for (size_t i = 0; i < edges.size(); ++i)
                    if (t >= edges[i].start && t < edges[i].start + edges[i].duration) candidates[count++] = i;
                const double choice = Rg.next();
                const auto&  e      = edges[count ? candidates[size_t(std::floor(choice * double(count)))] : 0];
                const double s      = clamp((t - e.start) / e.duration);
                x                   = e.p0.x + (e.p1.x - e.p0.x) * s;
                y                   = (e.p0.y + (e.p1.y - e.p0.y) * s) * SY;
            }
            emit({ParticleKind::Ember, x, y - 1, Rg.range(-12, 12), Rg.range(-30, -8), Rg.range(.25, .55), C[6], 1});
        }
        if (t > RUNES[1] - .1 && t < FLASH - .05 && Rg.isChance(.7)) {
            const double g = Rg.range(0, TAU), distance = Rg.range(R * .7, R * 1.1), life = Rg.range(.16, .24),
                         v = (distance * 3) / (std::exp(3 * life) - 1), x = std::cos(g) * distance,
                         y = std::sin(g) * distance * SY - 1, raw = jsHypot2(x, y + 2), len = raw == 0 ? 1 : raw;
            emit({ParticleKind::Streak, x, y, (-x / len) * v, ((-2 - y) / len) * v, life,
                  Rg.isChance(.5) ? C[6] : C[5]});
        }
        if (!flashed && t >= FLASH) {
            flashed = true;
            lights.add(0, -56, 104, C[5], .9, .6);
            lights.add(0, -5, 36, WHITE, .8, .14);
            for (int i = 0; i < 36; ++i) {
                const double side = Rg.sign(), sp = Rg.range(80, 175);
                emit({ParticleKind::Spark, side * Rg.range(0, R * .5), -1, side * sp, -Rg.range(30, 180),
                      Rg.range(.25, .7)});
            }
            for (int i = 0; i < 26; ++i)
                emit({ParticleKind::Star, Rg.range(-R, R), -Rg.range(1, 6), Rg.range(-20, 20), -Rg.range(40, 130),
                      Rg.range(.5, 1.1), Rg.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}),
                      Rg.range(.8, 2.3)});
            for (int i = 0; i < 34; ++i) {
                const double side = Rg.sign();
                emit({ParticleKind::Star, side * Rg.range(0, 14), -Rg.range(1, 4), side * Rg.range(50, 140),
                      -Rg.range(10, 90), Rg.range(.4, .95),
                      Rg.pick<uint32_t>(std::array<uint32_t, 4>{WHITE, C[6], C[6], C[5]}), Rg.range(.7, 2.2)});
            }
        }
        if (t > RUNES[0] && t < BREAK + .2 && Rg.isChance(.12)) {
            const double g = Rg.range(0, TAU), r = Rg.pick<double>(std::array<double, 3>{R, R2, RI});
            emit({ParticleKind::Star, std::cos(g) * r, std::sin(g) * r * SY - .5, 0, -Rg.range(2, 8),
                  Rg.range(.25, .45), Rg.pick<uint32_t>(std::array<uint32_t, 2>{WHITE, C[6]}), Rg.range(.8, 2)});
        }
        if (t > FLASH && t < BREAK) {
            if (Rg.isChance(.5))
                emit({ParticleKind::Star, Rg.range(-1, 1) * W * 1.5, -Rg.range(0, 120), Rg.range(-10, 10),
                      -Rg.range(30, 85), Rg.range(.5, 1.1),
                      Rg.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}), Rg.range(.7, 2)});
            if (Rg.isChance(.35))
                emit({ParticleKind::Mote, Rg.range(-1, 1) * W * .8, -Rg.range(0, 50), 0, -Rg.range(160, 300),
                      Rg.range(.2, .35), Rg.isChance(.5) ? WHITE : C[6], .8});
        }
        if (!broke && t >= BREAK) {
            broke = true;
            for (int i = 0; i < 46; ++i)
                emit({ParticleKind::Star, Rg.range(-1, 1) * W * .9, -Rg.range(2, TOP * .8), Rg.range(-12, 12),
                      -Rg.range(8, 40), Rg.range(.5, 1.15),
                      Rg.pick<uint32_t>(std::array<uint32_t, 4>{WHITE, WHITE, C[6], C[5]}), Rg.range(.6, 2.4)});
        }
        if (t > BREAK && t < BREAK + .4 && Rg.isChance(.8)) {
            emit({ParticleKind::Star, Rg.range(-1, 1) * W * .7, -Rg.range(2, TOP * .78), Rg.range(-8, 8),
                  -Rg.range(10, 50), Rg.range(.4, .9), Rg.pick<uint32_t>(std::array<uint32_t, 3>{WHITE, C[6], C[5]}),
                  Rg.range(.6, 1.8)});
            if (Rg.isChance(.4))
                emit({ParticleKind::Mote, Rg.range(-1, 1) * W * .5, -Rg.range(2, TOP * .7), 0, -Rg.range(60, 140),
                      Rg.range(.2, .4), C[6], .7});
        }
        for (int k = 0; k < NRUNE; ++k)
            if (!out[size_t(k)] && t >= runeOut(k)) {
                out[size_t(k)] = true;
                const double g = runeAngle(k), x = std::cos(g) * RR, y = std::sin(g) * RR * SY - 5;
                for (int i = 0; i < 4; ++i)
                    emit({ParticleKind::Star, x + Rg.range(-2, 2), y + Rg.range(-2, 2), Rg.range(-14, 14),
                          -Rg.range(10, 40), Rg.range(.3, .55), Rg.pick<uint32_t>(std::array<uint32_t, 2>{WHITE, C[6]}),
                          Rg.range(.6, 1.6)});
            }
        ps.update(dt, Rg, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double lvl = level(t);
        if (t < GONE) {
            const double k = lvl * smooth(PEN[0], PEN[1] + .2, t);
            p.glow(0, 0, R * 1.3, C[4], .28 * k);
            for (int side : {-1, 1}) p.glow(side * R * .6, 1, R * .7, C[5], .12 * k);
        }
        shock(p, true);
        circle(p, true);
        runes(p, true);
        column(p);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        shock(p, false);
        circle(p, false);
        runes(p, false);
        const auto pen = [&](double x, double y) {
            p.sparkle(x, y, 2, C[6], C[5], .5);
            p.glow(x, y, 7, C[6], .45);
        };
        if (t > PEN[0] && t < PEN[1])
            for (int side : {-1, 1}) {
                const double g = PI / 2 + side * ((t - PEN[0]) / (PEN[1] - PEN[0])) * PI;
                pen(std::cos(g) * R, std::sin(g) * R * SY);
            }
        if (t > INNER[0] && t < INNER[1]) {
            const double g = -PI / 2 + ((t - INNER[0]) / (INNER[1] - INNER[0])) * TAU;
            pen(std::cos(g) * RI, std::sin(g) * RI * SY);
        }
        for (const auto& e : edges)
            if (t > e.start && t < e.start + e.duration) {
                const double s = (t - e.start) / e.duration;
                pen(e.p0.x + (e.p1.x - e.p0.x) * s, (e.p0.y + (e.p1.y - e.p0.y) * s) * SY);
            }
        if (t >= FLASH && t < FLASH + .24) {
            const double a = (t - FLASH) / .24;
            flare(p, 0, -8, 34 * std::pow(1 - a, 1.5), 1 - a, C);
            p.glow(0, -26, 56, WHITE, .5 * (1 - a));
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& magicCircleCastDescriptor() {
    static const Descriptor d{
        {PixelFxKind::MagicCircleCast, "arcane/magic-circle-cast", 200, 220, 100, 192, END, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 6;
            s.palette = {hex(0x140a26), hex(0x2b1252), hex(0x481c88), hex(0x6b2cc0),
                         hex(0x9a48ec), hex(0xc684ff), hex(0xecc8ff), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<MagicCircleCast>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
