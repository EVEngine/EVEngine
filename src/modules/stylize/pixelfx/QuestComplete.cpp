// eve-hot-path
// Native adaptation of pixelfx ui/quest-complete.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LENGTH = 66, BH = 8, OPEN = .32, TYPE = .2, STEP = .036, STAMP = .95, SX = 64, SY = 4, POPSEAL = 2,
                 ROLL = 2.08, END = 2.6 - .05;
struct QuestSpark {
    double x, y, t0, duration, length;
};
struct QuestComplete final : Kernel {
    const std::vector<uint32_t> G, Rb;
    Rng                         rng;
    const uint32_t              seed;
    Particles                   ps;
    Lights                      lights;
    const ui::Word              word;
    std::vector<ui::Letter>     glyphs;
    mutable ui::WordScratch     scratch;
    std::array<QuestSpark, 7>   sats{};
    double                      t       = 0;
    bool                        stamped = false, popped = false;
    explicit QuestComplete(const PixelFxSettings& o)
        : G(o.palette),
          Rb(o.ui.ribbon),
          rng(o.seed),
          seed(o.seed),
          word(ui::spell(o.ui.text, PixelFont::Mini)),
          scratch(size_t((word.width + 8) * 3 + 64), 64) {
        ps.sparks = {G[7], G[6], G[5], G[3]};
        for (const auto& L : word.letters)
            if (L.glyph.width) glyphs.push_back(L);
        for (size_t i = 0; i < sats.size(); ++i) {
            const double x = (double(i) / 6 - .5) * 140 + rng.range(-6, 6), y = (i % 2 ? 1 : -1) * rng.range(15, 22),
                         t0 = STAMP + .12 + double(i) * .12 + rng.range(0, .04), duration = rng.range(.3, .45),
                         length = rng.range(3, 6);
            sats[i]             = {x, y, t0, duration, length};
        }
    }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 3> stars{G[7], G[6], G[5]};
        const std::array<uint32_t, 2> gold{G[6], G[5]}, bright{G[7], G[6]};
        if (!stamped && t >= STAMP) {
            stamped = true;
            lights.add(SX, SY, 40, G[5], .7, .35);
            lights.add(SX, SY, 18, WHITE, .7, .1);
            for (int i = 0; i < 12; ++i) {
                const double a     = (double(i) / 12) * TAU + rng.range(-.2, .2),
                             speed = rng.range(40, 80) * (std::cos(a) > 0 ? .6 : 1), life = rng.range(.25, .45);
                const auto   color = rng.pick<uint32_t>(stars);
                const double size  = i % 3 ? .6 : rng.range(1.2, 1.8);
                ps.spawn(rng, ParticleKind::Star, SX + std::cos(a) * 11, SY + std::sin(a) * 11, std::cos(a) * speed,
                         std::sin(a) * speed * .7, life, color, size);
            }
        }
        if (t > STAMP && t < POPSEAL && rng.isChance(.08)) {
            const double x = rng.range(-LENGTH, LENGTH), y = rng.range(-BH, BH), vx = rng.range(-5, 5),
                         vy = -rng.range(8, 20), life = std::min(rng.range(.4, .8), END - t);
            const auto   color = rng.pick<uint32_t>(gold);
            ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, color, .6);
        }
        if (!popped && t >= POPSEAL + .05) {
            popped = true;
            lights.add(SX, SY, 30, G[6], .6, .2);
            for (int i = 0; i < 8; ++i) {
                const double a = rng.range(0, TAU), speed = rng.range(40, 90),
                             life  = std::min(rng.range(.25, .4), END - t);
                const auto   color = rng.pick<uint32_t>(bright);
                ps.spawn(rng, ParticleKind::Star, SX + std::cos(a) * 6, SY + std::sin(a) * 6, std::cos(a) * speed,
                         std::sin(a) * speed, life, color, i % 2 ? .6 : 1.5);
            }
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void ribbon(Paint& p, double l, double tails) const {
        if (l < 1) return;
        if (tails > .02)
            for (double s : {-1., 1.}) {
                const double drop = 6 * tails, x0 = s * (l - 8), x1 = s * (l + 16 * tails);
                p.shader(std::min(x0, x1) - 1, -BH + drop - 1, std::max(x0, x1) + 1, BH + drop + 1,
                         [&](double x, double y, ShadeOutput& out) {
                             const double u = (x - x0) / (x1 - x0), v = y - drop;
                             if (u < 0 || u > 1 || std::abs(v) > BH) return;
                             if (u > 1 - (.3 * (BH - std::abs(v))) / BH) return;
                             const double e = std::min(BH - std::abs(v), (1 - u) * (x1 - x0) * s);
                             out.c          = e < 1 || u < .02 ? Rb[0] : v < -BH + 2 ? Rb[3] : v < 0 ? Rb[2] : Rb[1];
                         });
                p.tri(s * (l - 8), BH, s * (l - 1), BH, s * (l - 8), BH + drop, Rb[0]);
            }
        p.shader(-l - 1, -BH - 1, l + 1, BH + 1, [&](double x, double y, ShadeOutput& out) {
            const double ax = std::abs(x), v = y;
            if (ax > l || std::abs(v) > BH) return;
            const double e = std::min(l - ax, BH - std::abs(v));
            if (e < 1) {
                out.c = Rb[0];
                return;
            }
            if (v < -BH + 2) {
                out.c = Rb[5];
                return;
            }
            if (v > BH - 2) {
                out.c = Rb[1];
                return;
            }
            if ((std::abs(v + BH - 2.5) < .5 || std::abs(v - BH + 3.5) < .5) && std::fmod(std::floor(x + .5), 3) != 0 &&
                ax < l - 3) {
                out.c = G[3];
                return;
            }
            out.c = v < -1 ? Rb[4] : Rb[3];
        });
        if (tails < .02)
            for (double s : {-1., 1.}) {
                const double x = jsRound(s * l);
                p.rect(x - 2, -BH - 2, 4, BH * 2 + 4, Rb[0]);
                p.rect(x - 1, -BH - 1, 1, BH * 2 + 2, Rb[4]);
                p.rect(x, -BH - 1, 1, BH * 2 + 2, Rb[2]);
                p.px(x - 1, -BH - 1, Rb[6]);
            }
    }
    void seal(Paint& p, double x, double y, double r, double sx, double sy, bool white, double k) const {
        const double M = r * 1.25 + 2;
        p.shader(x - M * sx, y - M * sy, x + M * sx, y + M * sy, [&](double px, double py, ShadeOutput& out) {
            const double dx = (px - x) / sx, dy = (py - y) / sy, d = jsHypot2(dx, dy), a = std::atan2(dy, dx),
                         edge = r * (1 + .09 * std::cos(a * 14));
            if (d > edge) return;
            if (white) {
                out.c = edge - d < 1 ? G[5] : WHITE;
                return;
            }
            if (edge - d < 1) {
                out.c = G[0];
                return;
            }
            const double l = -(dx * .6 + dy * .8) / r;
            if (edge - d < 3) {
                out.c = l > .3 ? G[6] : l > -.3 ? G[5] : G[3];
                return;
            }
            if (std::abs(d - r * .66) < .5) {
                out.c = l > 0 ? G[3] : G[2];
                return;
            }
            out.c = l > .45 ? G[7] : l > .1 ? G[6] : l > -.35 ? G[5] : G[4];
        });
        if (white || k <= 0) return;
        const std::array<Point, 3> points{
            {{x - 5 * sx, y}, {x - 1.5 * sx, y + 3.5 * sy}, {x + 5.5 * sx, y - 4.5 * sy}}};
        const double len1 = jsHypot2(points[1].x - points[0].x, points[1].y - points[0].y),
                     len2 = jsHypot2(points[2].x - points[1].x, points[2].y - points[1].y), done = k * (len1 + len2);
        const std::array<double, 2> u{std::min(1., done / len1), clamp((done - len1) / len2)};
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < 2; ++i)
                if (u[i] > 0) {
                    const auto& a = points[i];
                    const auto& b = points[i + 1];
                    p.beam(a.x, a.y, a.x + (b.x - a.x) * u[i], a.y + (b.y - a.y) * u[i], pass ? 2.4 : 4.2,
                           pass ? WHITE : G[0]);
                }
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto   jitter = ui::shake(t, 2, STAMP, .18, seed);
        const double l     = LENGTH *
                             (t < ROLL ? ease::outCubic(seg(t, 0, OPEN)) : 1 - ease::inCubic(seg(t, ROLL, ROLL + .26))),
                     tails = seg(t, OPEN - .02, OPEN + .12) * (t < ROLL ? 1 : 0), sh = seg(t, 1.2, 1.5);
        const auto   shine =
            sh > 0 && sh < 1 ? std::optional<double>(-LENGTH - 10 + (2 * LENGTH + 20) * sh) : std::nullopt;
        ribbon(p, l, ease::outBack(tails, 2));
        if (shine)
            for (int y = int(-BH + 1); y < BH; ++y) {
                const double x = *shine + (y + BH) * .6;
                if (std::abs(x) < l - 2) {
                    p.px(x, y, Rb[6]);
                    p.px(x + 1, y, Rb[5]);
                }
            }
        ui::WordItems items;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            const auto&  L = glyphs[i];
            const double u = t - (TYPE + double(i) * STEP), x = -word.width - 4 + L.x * 2 + jitter.x;
            if (u < 0 || std::abs(x + L.width) > l - 3) continue;
            const double s = u < .035 ? 2.6 : 2;
            items.add({L.glyph, L.height, x - ((s - 2) * L.width) / 2, -5 + jitter.y - ((s - 2) * 5) / 2, s, s, 1,
                       u < .07 ? 1. : 0.});
        }
        const std::array<uint32_t, 4> fill{G[4], G[5], G[6], G[7]};
        ui::WordStyle                 style{fill};
        style.edge    = WHITE;
        style.under   = G[3];
        style.outline = Rb[0];
        style.shadow  = Rb[1];
        scratch.draw(p, items.view(), style);
        const double dk = seg(t, STAMP - .12, STAMP), pk = t - POPSEAL;
        if (dk > 0 && pk < .08) {
            const double fall = 1 - ease::inQuad(dk), sq = std::exp(-(t - STAMP) * 30) * (t >= STAMP ? 1 : 0),
                         s  = 1 + .8 * fall;
            double       sx = s * (1 + .2 * sq), sy = s * (1 - .2 * sq);
            if (pk > 0) sx = sy = 1 + .3 * (pk / .08);
            const bool white = (t >= STAMP && t < STAMP + .05) || pk > 0;
            if (dk < 1) p.ellipse(SX, SY + 12, 10 * (1 - .3 * fall), 2, Rb[0], .5 * dk);
            seal(p, SX + jitter.x, SY - 14 * fall + jitter.y, 11, sx, sy, white, seg(t, STAMP + .08, STAMP + .24));
            p.glow(SX, SY, 22, G[5], .3 + (white ? .4 : 0));
        }
        const double rk = (t - STAMP) / .3, rr = 12 + 13 * ease::outCubic(rk);
        if (rk > 0 && rk < 1)
            p.ellipse(SX, SY, rr * 1.15, rr, rk < .3 ? WHITE : G[5],
                      [&](double d) { return d > 1 - 1.4 / rr ? 1.2 - rk : 0.; });
        for (const auto& s : sats) {
            const double u = (t - s.t0) / s.duration;
            if (u > 0 && u < 1) p.sparkle(s.x, s.y, s.length * std::pow(std::sin(u * PI), .7), G[6], G[5], .5);
        }
        ps.draw(p);
        p.glow(0, 0, 76, G[4], .2 * seg(t, .2, .5) * (1 - seg(t, ROLL, ROLL + .2)));
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& questCompleteDescriptor() {
    static const std::array<Option, 2> options{
        {{"ribbon", 1, 64, false, 7, &PixelFxUiSettings::ribbon}, {"text", 0, 64, false, 0, &PixelFxUiSettings::text}}};
    static const Descriptor d{
        {PixelFxKind::QuestComplete, "ui/quest-complete", 204, 92, 102, 46, 2.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed      = 8;
            s.ui.text   = "QUEST COMPLETE";
            s.palette   = {hex(0x2a1405), hex(0x5c3008), hex(0x9a5a0e), hex(0xd48e18),
                           hex(0xf5b92c), hex(0xffde5e), hex(0xfff4bc), WHITE};
            s.ui.ribbon = {hex(0x1e0608), hex(0x45101a), hex(0x7a1a26), hex(0xad2632),
                           hex(0xd8433f), hex(0xf27a5e), hex(0xffb79a)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<QuestComplete>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
