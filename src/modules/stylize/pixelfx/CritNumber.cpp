// eve-hot-path
// Native adaptation of pixelfx ui/crit-number.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double FOLD = .92, TAGOUT = .98, POP = 1.06, SX = 1.3, NS = 4, NY = 5, END = 1.5 - .05;
struct CritTip {
    double length, dip;
};
struct CritLine {
    double angle, radius, length;
};
struct CritNumber final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const uint32_t              shadow, seed;
    const ui::Word              number, label;
    mutable ui::WordScratch     scratch;
    std::array<CritTip, 13>     tips;
    std::array<CritLine, 18>    lines;
    Particles                   ps;
    Bits                        bits;
    Lights                      lights;
    double                      t       = 0;
    bool                        slammed = false, stamped = false, popped = false, tagGone = false;
    explicit CritNumber(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          shadow(scale(C[0], .5)),
          seed(o.seed),
          number(ui::spell(o.ui.text)),
          label(ui::spell(o.ui.label, PixelFont::Mini)),
          scratch(size_t((std::max(number.width, label.width) + 8) * 8 + 64), 128) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        for (auto& tip : tips) {
            const double length = R.range(.86, 1.12), dip = R.range(-.06, .06);
            tip = {length, dip};
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            const double a = (double(i) / 18) * TAU + R.range(-.1, .1), r = R.range(31, 34), length = R.range(8, 15);
            lines[i] = {a, r, length};
        }
    }
    static double reach(double v, double d) { return v <= d * 2.2 ? 9 : -std::log(1 - (d * 2.2) / v) / 2.2; }
    void          spawnStar(double x, double y, double vx, double vy, double life, uint32_t color, double size) {
        ps.spawn(R, ParticleKind::Star, x, y, vx, vy, std::min(life, END - t), color, size);
    }
    void update(double dt) override {
        t += dt;
        if (!slammed) {
            slammed = true;
            lights.add(0, 0, 70, WHITE, .9, .16);
            lights.add(0, 0, 60, C[4], .45, .5);
            for (int i = 0; i < 30; ++i) {
                const double a = R.range(0, TAU), speed = R.range(140, 300),
                             life = std::min(R.range(.12, .32), reach(speed * 1.15, 40));
                ps.spawn(R, ParticleKind::Spark, std::cos(a) * 8, std::sin(a) * 6, std::cos(a) * speed * 1.15,
                         std::sin(a) * speed - 40, life);
            }
            const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
            for (int i = 0; i < 10; ++i) {
                const double a = R.range(0, TAU), speed = R.range(90, 170);
                Debris       b;
                b.kind            = DebrisKind::Shard;
                b.x               = std::cos(a) * 10;
                b.y               = std::sin(a) * 7;
                b.vx              = std::cos(a) * speed * 1.15;
                b.vy              = std::sin(a) * speed - 60;
                b.gravity         = .35;
                b.drag            = 1.6;
                b.life            = std::min(R.range(.3, .5), END - t);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-24, 24);
                b.size            = R.range(2, 3.5);
                b.color           = R.pick<uint32_t>(colors);
                bits.add(std::move(b));
            }
        }
        if (!stamped && t >= .09) {
            stamped = true;
            lights.add(-4, -20, 30, C[6], .5, .2);
            for (int i = 0; i < 6; ++i) {
                const double x = -4 + R.range(-16, 16), y = -20 + R.range(-4, 4), vx = R.range(-40, 40),
                             vy = R.range(-30, 10), life = R.range(.2, .4), size = R.range(.5, 1.4);
                ps.spawn(R, ParticleKind::Star, x, y, vx, vy, life, C[6], size);
            }
        }
        if (!tagGone && t >= TAGOUT) {
            tagGone = true;
            const std::array<uint32_t, 2> colors{C[7], C[6]};
            for (int i = 0; i < 6; ++i) {
                const double x = -10 + R.range(-16, 16), y = -14 + R.range(-5, 5), vx = R.range(-50, 50),
                             vy = -R.range(10, 60), life = R.range(.25, .4);
                const auto   color = R.pick<uint32_t>(colors);
                spawnStar(x, y, vx, vy, life, color, i ? .6 : 1.6);
            }
        }
        if (t > .3 && t < FOLD - .1 && R.isChance(.07)) {
            const double a = R.range(0, TAU), speed = R.range(70, 150),
                         life = std::min(R.range(.12, .26), reach(speed, 14));
            ps.spawn(R, ParticleKind::Spark, std::cos(a) * 30 * SX, std::sin(a) * 30, std::cos(a) * speed,
                     std::sin(a) * speed - 30, life);
        }
        if (t > .12 && t < FOLD && R.isChance(.16)) {
            const double a = R.range(0, TAU), r = R.range(14, 28), vx = R.range(-10, 10), vy = -R.range(15, 40),
                         life = std::min(R.range(.5, .9), END - t), size = R.range(1, 2);
            ps.spawn(R, ParticleKind::Ember, std::cos(a) * r * SX, std::sin(a) * r, vx, vy, life, C[5], size);
        }
        if (!popped && t >= POP + .05) {
            popped = true;
            lights.add(0, 0, 44, C[6], .7, .25);
            const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
            for (int i = 0; i < 14; ++i) {
                const double a = (double(i) / 14) * TAU + R.range(-.15, .15), speed = R.range(60, 130),
                             life  = std::min(R.range(.3, .42), END - t);
                const auto   color = R.pick<uint32_t>(colors);
                const double size  = i % 3 ? .6 : R.range(1.3, 2.2);
                ps.spawn(R, ParticleKind::Star, std::cos(a) * 10 * SX, std::sin(a) * 7, std::cos(a) * speed * SX,
                         std::sin(a) * speed - 10, life, color, size);
            }
            for (int i = 0; i < 8; ++i) {
                const double a = R.range(0, TAU), speed = R.range(120, 240), sample = R.range(.15, .3),
                             life = std::min({sample, END - t, reach(speed, 40)});
                ps.spawn(R, ParticleKind::Spark, std::cos(a) * 6, std::sin(a) * 4, std::cos(a) * speed,
                         std::sin(a) * speed - 30, life);
            }
        }
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        bits.update(dt);
        lights.update(dt);
    }
    void starburst(Paint& p, double cx, double cy, double radius, double rot, double heat, uint32_t boil) const {
        if (radius < 2) return;
        std::array<Point, 26> vertices;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const double a = rot + (double(i) * PI) / 13,
                         r = radius * (i % 2 ? .6 + tips[i >> 1].dip
                                             : tips[i >> 1].length * (1 + .14 * (rand({boil, uint32_t(i)}) - .5)));
            vertices[i]    = {std::cos(a) * r, std::sin(a) * r};
        }
        const double step = PI / 13;
        const auto   edge = [&](double dx, double dy) {
            double a = std::atan2(dy, dx) - rot;
            a -= std::floor(a / TAU) * TAU;
            const size_t i = std::min(vertices.size() - 1, size_t(std::floor(a / step)));
            const auto   v = vertices[i], w = vertices[(i + 1) % vertices.size()];
            const double ex = w.x - v.x, ey = w.y - v.y, den = dx * ey - dy * ex;
            return std::abs(den) < 1e-9 ? jsHypot2(v.x, v.y) : (v.x * ey - v.y * ex) / den;
        };
        const double M = radius * 1.15 + 2;
        p.shader(cx - M * SX, cy - M, cx + M * SX, cy + M, [&](double x, double y, ShadeOutput& out) {
            const double dx = (x - cx) / SX, dy = y - cy, d = jsHypot2(dx, dy);
            if (d < .01) {
                out.c = WHITE;
                return;
            }
            const double L = edge(dx / d, dy / d), gap = (L - d) * (1 + .3 * std::abs(dx / d)), q = d / L;
            if (gap < 0) return;
            if (heat > .85) {
                out.c = gap < 1 ? C[6] : WHITE;
                return;
            }
            if (gap < 1) {
                out.c = C[1];
                return;
            }
            if (gap < 2.2) {
                out.c = heat > .3 ? WHITE : C[6];
                return;
            }
            if (gap < 3.4) {
                out.c = heat > .3 ? C[6] : C[5];
                return;
            }
            if (gap >= 6 && gap < 7) {
                out.c = heat > .5 ? WHITE : C[4];
                return;
            }
            const double a  = std::atan2(dy, dx) - rot,
                         sp = std::abs(std::fmod((a / TAU) * 13 + .5, 1) - .5) * (TAU / 13) * d;
            if (sp < .6 && q > .25) {
                out.c = heat > .5 ? WHITE : q > .6 ? C[5] : C[4];
                return;
            }
            const int                     i = int(jsRound((q > .72 ? 3 : q > .45 ? 4 : 5) - heat * 5));
            const std::array<uint32_t, 6> colors{WHITE, C[6], C[5], C[4], C[3], C[2]};
            out.c = i <= 0 ? WHITE : colors[size_t(i)];
        });
    }
    void tag(Paint& p, double x, double y, double w, double h, bool white) const {
        p.shader(x - 1, y - 1, x + w + 4, y + h + 1, [&](double px, double py, ShadeOutput& out) {
            const double lx = px - x - 3 * ((y + h - py) / h);
            if (py < y || py > y + h || lx < 0 || lx > w) return;
            if (white) {
                out.c = WHITE;
                return;
            }
            out.c = lx < 1 || lx > w - 1 || py < y + 1 || py > y + h - 1 ? C[0]
                    : py < y + 2                                         ? C[4]
                    : py < y + h * .55                                   ? C[3]
                                                                         : C[2];
        });
    }
    void speedLines(Paint& p) const {
        const double k = t / .24;
        if (k <= 0 || k >= 1) return;
        for (const auto& line : lines) {
            const double r0 = line.radius * (1 + .5 * ease::outExpo(k)), r1 = r0 + line.length * (1 - k) + 2,
                         ca = std::cos(line.angle) * 1.2, sa = std::sin(line.angle) * .92;
            p.lineFn(ca * r0, sa * r0, ca * r1, sa * r1, [&](double u) { return u > .5 || k < .3 ? WHITE : C[6]; }, 1.);
        }
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto   offset = ui::shake(t, 4, 0, .32, seed), labelOffset = ui::shake(t, 2, .09, .15, seed + 3);
        const double grow = t < .04 ? 1.25 : 1 + .1 * std::exp(-(t - .04) * 14),
                     fold = 1 - ease::inBack(seg(t, FOLD - .12, FOLD), 2.4),
                     heat = t < .035 ? 1 : clamp(1 - (t - .035) / .12), breathe = 1 + .04 * std::sin(t * 16);
        if (fold > .02)
            starburst(p, offset.x, offset.y, 31 * grow * fold * breathe, -PI / 2 + .25 * t, heat,
                      t < .2 ? 0 : jsUint(std::floor(t * 10)));
        speedLines(p);
        const double rk = t / .26, rr = 30 + 22 * ease::outCubic(rk);
        if (rk > 0 && rk < 1)
            p.ellipse(0, 0, rr * SX, rr, rk < .35 ? WHITE : C[6],
                      [&](double d) { return d > 1 - 1.6 / rr ? 1.2 - rk : 0.; });
        bits.draw(p);
        ps.draw(p);
        const double s  = NS + 3 * std::exp(-t * 26) * std::cos(t * 38);
        double       sx = s, sy = s, flash = t < .035 ? 1 : t < .07 ? .5 : 0;
        const double pu = (t - POP) / .05;
        if (pu >= 1) {
            const double k = (t - POP - .05) / .22;
            if (k > 0 && k < 1) {
                const double r = 6 + 22 * ease::outCubic(k);
                p.ellipse(0, 0, r * SX, r, k < .4 ? WHITE : C[6],
                          [&](double d) { return d > 1 - 1.5 / (6 + 22 * ease::outCubic(k)) ? 1.3 - k : 0.; });
            }
        } else {
            const double sq = seg(t, POP - .07, POP);
            if (sq > 0 && pu <= 0) {
                sx = NS * (1 + .14 * sq);
                sy = NS * (1 - .12 * sq);
            }
            if (pu > 0) {
                sx = sy = NS * (1 + .25 * pu);
                flash   = 1;
            }
            const double  W = number.width * sx, H = 7 * sy, x0 = offset.x - W / 2 - 3,
                          y0 = offset.y + NY - H / 2 - 3 * smooth(.15, FOLD, t);
            ui::WordItems items;
            for (const auto& L : number.letters)
                if (L.glyph.width) items.add({L.glyph, L.height, x0 + L.x * sx, y0, sx, sy, 1, flash, 1});
            const double sh = seg(t, .36, .6);
            const auto   shine =
                sh > 0 && sh < 1 ? std::optional<double>(x0 - 10 + (W + 26) * ease::inOutSine(sh)) : std::nullopt;
            const std::array<uint32_t, 4> fill{C[5], C[6], C[6], C[7]};
            ui::WordStyle                 style{fill};
            style.edge         = WHITE;
            style.under        = C[4];
            style.outline      = C[0];
            style.outlineWidth = 2;
            style.shadow       = shadow;
            style.shine        = shine;
            style.shineY       = y0;
            style.glint        = C[5];
            scratch.draw(p, items.view(), style);
            if (shine && *shine > x0 && *shine < x0 + W) p.glowLine(*shine, y0, *shine + .6 * H, y0 + H, 8, C[6], .3);
            const double lk = t - .06, lo = seg(t, TAGOUT - .05, TAGOUT);
            if (lk > 0 && t < TAGOUT) {
                const double ls = lk < .035  ? 3
                                  : lk < .07 ? 1.8
                                             : 2 * (1 + .15 * lo),
                             LW = label.width * ls, lh = 5 * ls, lx = offset.x + labelOffset.x - 8 - LW / 2,
                             ly = y0 - 3 - lh + labelOffset.y - 2 * lo;
                tag(p, lx - 4, ly - 3, LW + 8, lh + 5, lo > .5);
                ui::WordItems letters;
                for (const auto& L : label.letters)
                    if (L.glyph.width)
                        letters.add({L.glyph, L.height, lx + 1 + L.x * ls, ly, ls, ls, 1, lk < .035 ? 1. : 0., .5});
                const std::array<uint32_t, 3> tagFill{C[6], C[7], C[7]};
                ui::WordStyle                 tagStyle{tagFill};
                tagStyle.edge   = WHITE;
                tagStyle.under  = C[5];
                tagStyle.shadow = C[1];
                scratch.draw(p, letters.view(), tagStyle);
            }
            const double g = (t - .6) / .24;
            if (g > 0 && g < 1) p.sparkle(x0 + W - 2, y0 + 1, 6 * std::sin(g * PI), C[6], C[5], .6);
        }
        const double hold = smooth(0, .1, t) * (1 - smooth(FOLD - .1, POP + .1, t));
        p.glow(offset.x, offset.y, 56, C[4], .14 * hold * (1 + .2 * std::sin(t * 16)));
        if (t < .12) p.glow(0, 0, 64, WHITE, .8 * (1 - t / .12));
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& critNumberDescriptor() {
    static const std::array<Option, 2> options{
        {{"text", 0, 64, false, 0, &PixelFxUiSettings::text}, {"label", 0, 64, false, 0, &PixelFxUiSettings::label}}};
    static const Descriptor d{
        {PixelFxKind::CritNumber, "ui/crit-number", 152, 120, 76, 60, 1.5, false},
        8,
        [](PixelFxSettings& s) {
            s.seed     = 3;
            s.ui.text  = "128!";
            s.ui.label = "CRIT";
            s.palette  = {hex(0x2a0710), hex(0x5e0f16), hex(0xa3191c), hex(0xe0401c),
                          hex(0xff7b22), hex(0xffb93b), hex(0xffe68a), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CritNumber>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
