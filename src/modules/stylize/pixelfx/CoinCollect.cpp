// eve-hot-path
// Native adaptation of pixelfx ui/coin-collect.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiHud.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double POP = .3, FLY0 = .58, GAP = .075, FLY = .28, FOLD = 2, CY = -16, HX = 50, HY = -88, HW = 60, HH = 22,
                 IX = 62, IY = -77, END = 2.4;
const ui::PanelColors INK{hex(0x0d0a14), hex(0x4a4060), hex(0x2a2338), hex(0x1e1929), hex(0x141019)};
struct FlyingCoin {
    double               x = 0, y = CY, vx = 0, vy = 0, phase = 0, spin = 0;
    int                  state = 0;
    double               t0 = 0, fx = 0, fy = 0;
    std::array<Point, 7> trail{};
    size_t               trailCount = 0;
    double               bob        = 0;
};
struct CoinCollect final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    const int                   N;
    std::array<FlyingCoin, 12>  coins{};
    std::vector<ui::Word>       words;
    mutable ui::WordScratch     scratch{512, 128};
    size_t                      arrivals = 0;
    double                      t = 0, bump = 0, flash = 0;
    bool                        popped = false;
    explicit CoinCollect(const PixelFxSettings& o)
        : C(o.palette), rng(o.seed), N(int(clamp(jsRound(o.ui.count), 1., 12.))) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        for (int i = 0; i < N; ++i) {
            auto&        c = coins[size_t(i)];
            const double s = (double(i) / std::max(1, N - 1)) * 2 - 1, vx = rng.range(55, 90),
                         jitter = rng.range(-12, 12);
            c.vx                = s * vx + jitter;
            c.vy                = -rng.range(170, 285);
            c.phase             = rng.range(0, TAU);
            const double spin   = rng.range(9, 14);
            c.spin              = spin * rng.sign();
            c.t0                = FLY0 + i * GAP;
            c.bob               = rng.range(0, TAU);
        }
        const auto start = int64_t(jsRound(o.ui.coinFrom));
        words.reserve(size_t(N + 1));
        for (int i = 0; i <= N; ++i) words.push_back(ui::spell("x" + std::to_string(start + i)));
    }
    void coin(Paint& p, double x, double y, double r, double w, bool white = false) const {
        const double rx = std::max(.7, r * w), ry = r;
        p.shader(x - rx - 1, y - ry - 1, x + rx + 1, y + ry + 1, [&](double px, double py, ShadeOutput& out) {
            const double u = (px - x) / rx, v = (py - y) / ry, d = jsHypot2(u, v);
            if (d > 1) return;
            const double e = (1 - d) * std::min(rx * 1.6, ry);
            if (white) {
                out.c = e < 1 ? C[6] : WHITE;
                return;
            }
            if (e < 1) {
                out.c = C[1];
                return;
            }
            if (rx < 2) {
                out.c = v < -.2 ? C[6] : v < .4 ? C[4] : C[3];
                return;
            }
            if (e < 2) {
                out.c = u + v < -.2 ? C[6] : C[3];
                return;
            }
            if (std::abs(px - x) < .55 && std::abs(v) < .5) {
                out.c = C[3];
                return;
            }
            if (px - x >= .55 && px - x < 1.55 && std::abs(v) < .5) {
                out.c = C[7];
                return;
            }
            const double l = -(u * .6 + v * .8);
            out.c          = l > .4 ? C[7] : l > .05 ? C[6] : l > -.35 ? C[5] : C[4];
        });
    }
    void update(double dt) override {
        t += dt;
        const std::array<uint32_t, 3> stars{C[7], C[6], C[5]};
        const std::array<uint32_t, 2> bright{C[7], C[6]};
        if (!popped && t >= POP) {
            popped = true;
            lights.add(0, CY, 46, WHITE, .8, .14);
            lights.add(0, CY, 48, C[5], .6, .5);
            for (int i = 0; i < 14; ++i) {
                const double a = (double(i) / 14) * TAU + rng.range(-.2, .2), speed = rng.range(80, 170),
                             life = rng.range(.15, .35);
                ps.spawn(rng, ParticleKind::Spark, std::cos(a) * 3, CY + std::sin(a) * 3, std::cos(a) * speed,
                         std::sin(a) * speed - 30, life);
            }
            for (int i = 0; i < 4; ++i) {
                const double x = rng.range(-6, 6), sign = rng.sign(), vx = sign * rng.range(15, 40),
                             vy = -rng.range(2, 8), life = rng.range(.3, .5), size = rng.range(1.5, 2.5);
                ps.spawn(rng, ParticleKind::Dust, x, -1, vx, vy, life, hex(0x5a4f6e), size);
            }
            for (int i = 0; i < 10; ++i) {
                const double a = (double(i) / 10) * TAU + rng.range(-.2, .2), speed = rng.range(70, 130),
                             down = std::sin(a) > 0 ? .25 : 1, life = rng.range(.35, .6);
                const auto   color = rng.pick<uint32_t>(stars);
                const double size  = i % 3 ? .6 : rng.range(1.2, 2);
                ps.spawn(rng, ParticleKind::Star, std::cos(a) * 6, CY + std::sin(a) * 6, std::cos(a) * speed,
                         std::sin(a) * speed * down, life, color, size);
            }
        }
        for (int j = 0; j < N; ++j) {
            auto& c = coins[size_t(j)];
            if (!popped || c.state == 3) continue;
            c.phase += c.spin * dt;
            if (c.state < 2) {
                if (c.state == 0) {
                    c.vy += 640 * dt;
                    c.vx -= c.vx * 2.2 * dt;
                    if (c.vy > 0) c.state = 1;
                } else {
                    c.vy -= c.vy * 7 * dt;
                    c.vx -= c.vx * 4 * dt;
                }
                c.x += c.vx * dt;
                c.y += c.vy * dt;
                if (t >= c.t0) {
                    c.state = 2;
                    c.fx    = c.x;
                    c.fy    = c.y + std::sin(t * 6 + c.bob) * 1.5;
                }
            }
            if (c.state == 2) {
                const double u = clamp((t - c.t0) / FLY), e = u * u, mx = c.fx + (IX - c.fx) * .35,
                             my = std::min(c.fy, IY) - 26;
                c.x             = (1 - e) * (1 - e) * c.fx + 2 * (1 - e) * e * mx + e * e * IX;
                c.y             = (1 - e) * (1 - e) * c.fy + 2 * (1 - e) * e * my + e * e * IY;
                c.trailCount    = std::min(size_t(7), c.trailCount + 1);
                for (size_t i = c.trailCount - 1; i > 0; --i) c.trail[i] = c.trail[i - 1];
                c.trail[0] = {c.x, c.y};
                if (rng.isChance(.25)) {
                    const double x = c.x + rng.range(-2, 2), y = c.y + rng.range(-2, 2), vx = rng.range(-8, 8),
                                 vy = rng.range(-4, 10), life = rng.range(.2, .4);
                    const auto   color = rng.pick<uint32_t>(bright);
                    ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, color, .6);
                }
                if (u >= 1) {
                    c.state = 3;
                    ++arrivals;
                    bump  = 1;
                    flash = 1;
                    lights.add(IX, IY, 26, C[5], .6, .25);
                    for (int i = 0; i < 3; ++i) {
                        const double a = rng.range(0, TAU), speed = rng.range(18, 40),
                                     life  = std::min(rng.range(.25, .45), END - t);
                        const auto   color = rng.pick<uint32_t>(bright);
                        const double size  = i ? .6 : rng.range(1.2, 1.8);
                        ps.spawn(rng, ParticleKind::Star, IX + std::cos(a) * 5, IY + std::sin(a) * 5,
                                 std::cos(a) * speed, std::sin(a) * speed, life, color, size);
                    }
                }
            }
        }
        bump  = std::max(0., bump - dt / .14);
        flash = std::max(0., flash - dt / .07);
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void floorBand(Paint& p, bool back) const {
        const double k = (t - POP) / .45;
        if (k > 0 && k < 1)
            band(p, 0, 0, 4 + 34 * ease::outCubic(k), .22, std::max(1., 3 * (1 - k)), 1.15 - k, C,
                 [&](double g) { return back ? std::sin(g) < 0 : std::sin(g) >= 0; });
    }
    void drawBack(Paint& p) const override {
        floorBand(p, true);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        floorBand(p, false);
        if (t < POP + .02) {
            const double y = CY + std::sin(t * 9) * 1.5, w = std::abs(std::cos(t * 7));
            p.ellipse(0, .5, 6 - .5 * std::sin(t * 9), 1.3, hex(0x0d0a14), .6);
            for (int i = 0; i < 2; ++i) {
                const double a = t * 7 + i * PI, ox = std::cos(a) * 11, oy = std::sin(a) * 3;
                if (std::sin(a) < 0) p.px(ox, y + oy, C[4]);
            }
            coin(p, 0, y, 7, std::max(.12, w), t > POP - .04);
            for (int i = 0; i < 2; ++i) {
                const double a = t * 7 + i * PI, ox = std::cos(a) * 11, oy = std::sin(a) * 3;
                if (std::sin(a) >= 0) {
                    p.px(ox, y + oy, C[7]);
                    p.glow(ox, y + oy, 4, C[5], .4);
                }
            }
            p.glow(0, y, 20, C[5], .45 + .4 * seg(t, POP - .15, POP));
            if (t > .06 && t < .2) p.sparkle(-3, y - 3, 3.5 * std::sin(((t - .06) / .14) * PI), C[7], C[6], .4);
        }
        const double pk = (t - POP) / .1;
        if (pk > 0 && pk < 1) p.circle(0, CY, 13 * (1 - pk) + 1, pk < .5 ? WHITE : C[6]);
        const double fk = (t - POP) / .2;
        if (fk > 0 && fk < 1) flare(p, 0, CY, 26 * std::pow(1 - fk, 1.5), 1 - fk, C);
        const double rk = (t - POP - .03) / .35;
        if (rk > 0 && rk < 1)
            p.ring(0, CY, 6 + 22 * ease::outCubic(rk), std::max(1., 2 * (1 - rk)), rk < .3 ? WHITE : C[6], 1.2 - rk);
        for (int j = 0; j < N; ++j) {
            const auto& c = coins[size_t(j)];
            if (!popped || c.state == 3) continue;
            for (size_t i = 1; i < c.trailCount; ++i)
                p.line(c.trail[i - 1].x, c.trail[i - 1].y, c.trail[i].x, c.trail[i].y, i < 3 ? C[6] : C[4],
                       1.2 - double(i) / 7);
            const double hover = c.state == 1 ? std::sin(t * 6 + c.bob) * 1.5 : 0, w = std::abs(std::cos(c.phase));
            coin(p, c.x, c.y + hover, 5, std::max(.15, w));
            p.glow(c.x, c.y + hover, 10, C[5], c.state == 2 ? .5 : .32);
            const double gl = std::fmod(t * 1.7 + c.bob, 1);
            if (c.state == 1 && gl < .12 && w > .5)
                p.sparkle(c.x - 1, c.y + hover - 2, 3 * std::sin((gl / .12) * PI), C[7], C[6], .4);
        }
        const double open = ease::outBack(seg(t, .06, .26), 2.2) * (1 - ease::inBack(seg(t, FOLD, FOLD + .2), 2));
        if (open > .04) {
            const double h = HH * open, y0 = HY + (HH - h) / 2, sh = seg(t, 1.5, 1.76);
            ui::panel(p, HX, y0, HW, h, INK, std::min(4., h / 2), 1,
                      sh > 0 && sh < 1 ? std::optional<double>(HX - 6 + (HW + 16) * sh) : std::nullopt);
            if (open > .7) {
                coin(p, IX, IY, 5.5 * (1 + .3 * bump), 1, flash > .5);
                p.glow(IX, IY, 14, C[5], .25 + .4 * bump);
                if (bump > 0) p.ring(IX, IY, 7 + 8 * (1 - bump), 1, bump > .6 ? WHITE : C[6], bump * 1.3);
                const double  s = 2 * (1 + .22 * bump), x0 = IX + 9, ty = IY - 7 * s / 2;
                const auto&   word = words[arrivals];
                ui::WordItems items;
                for (const auto& L : word.letters)
                    if (L.glyph.width) items.add({L.glyph, L.height, x0 + L.x * s, ty, s, s, 1, flash > .5 ? 1. : 0.});
                const std::array<uint32_t, 4> fill{C[4], C[5], C[6], C[7]};
                ui::WordStyle                 style{fill};
                style.edge    = WHITE;
                style.under   = C[3];
                style.outline = INK.outline;
                style.shadow  = 0;
                scratch.draw(p, items.view(), style);
                if (bump > 0) p.glow(x0 + word.width * s / 2, IY, 16, C[5], .3 * bump);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& coinCollectDescriptor() {
    static const std::array<Option, 2> options{{{"count", 1, 12, false, 0, &PixelFxUiSettings::count},
                                                {"from", -999999, 999999, false, 0, &PixelFxUiSettings::coinFrom}}};
    static const Descriptor            d{
        {PixelFxKind::CoinCollect, "ui/coin-collect", 176, 116, 60, 102, 2.45, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 4;
            s.ui.count    = 10;
            s.ui.coinFrom = 12;
            s.palette     = {hex(0x2a1405), hex(0x5c3008), hex(0x9a5a0e), hex(0xd48e18),
                             hex(0xf5b92c), hex(0xffde5e), hex(0xfff4bc), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CoinCollect>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
