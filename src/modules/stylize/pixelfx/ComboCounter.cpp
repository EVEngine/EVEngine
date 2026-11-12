// eve-hot-path
// Native adaptation of pixelfx ui/combo-counter.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double HIT = .1, BEAT = .5, OUT = 1.1, BS = 4, LS = 2, BASE = 12;
struct ComboSpike {
    double angle, length;
};
struct ComboCounter final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const uint32_t              shadow, seed;
    const ui::Word              big, label;
    const double                W, X0;
    mutable ui::WordScratch     scratch;
    Particles                   ps;
    Lights                      lights;
    std::array<ComboSpike, 10>  spikes;
    double                      t   = 0;
    bool                        hit = false, out = false;
    explicit ComboCounter(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          shadow(scale(C[0], .5)),
          seed(o.seed),
          big(ui::spell("x" + std::to_string(int64_t(jsRound(o.ui.count))))),
          label(ui::spell(o.ui.label)),
          W(big.width * BS + 4 + label.width * LS),
          X0(-W / 2),
          scratch(size_t((big.width + label.width + 16) * 8 + 128), 128) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        for (size_t i = 0; i < spikes.size(); ++i) {
            const double a = (double(i) / 10) * TAU + R.range(-.15, .15), len = R.range(14, 24);
            spikes[i] = {a, len};
        }
    }
    double xAt(double time) const {
        time = std::max(0., time);
        if (time < HIT) return -32 * std::pow(1 - time / HIT, 2);
        if (time < OUT) return 7 * std::exp(-(time - HIT) * 18) * std::sin((time - HIT) * 34);
        const double u = seg(time, OUT, OUT + .2);
        return u < .3 ? -4 * ease::outQuad(u / .3) : -4 + 24 * ease::inQuad((u - .3) / .7);
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, 0, 70, C[5], .7, .4);
            lights.add(-20, 0, 34, WHITE, .8, .1);
            for (int i = 0; i < 26; ++i) {
                const double a0 = R.range(-1.2, 1.2), a = a0 + (R.isChance(.3) ? PI : 0), speed = R.range(120, 300),
                             x = R.range(-20, 20), y = R.range(-8, 8), life = R.range(.12, .3);
                ps.spawn(R, ParticleKind::Spark, x, y, std::cos(a) * speed, std::sin(a) * speed * .6 - 20, life);
            }
        }
        const std::array<uint32_t, 2> colors{C[7], C[6]};
        if (t > BEAT && t < OUT && R.isChance(.1)) {
            const double x = X0 + R.range(0, big.width * BS), y = BASE - R.range(0, 30), vx = R.range(-10, 10),
                         vy = -R.range(5, 20), life = R.range(.25, .45);
            const auto   color = R.pick<uint32_t>(colors);
            const double size  = R.isChance(.3) ? 1.6 : .6;
            ps.spawn(R, ParticleKind::Star, x, y, vx, vy, life, color, size);
        }
        if (!out && t >= OUT + .06) {
            out = true;
            for (int i = 0; i < 10; ++i) {
                const double x = R.range(-50, 30), y = R.range(-14, 10), vx = R.range(220, 320), vy = R.range(-10, 10),
                             life  = R.range(.06, .14);
                const auto   color = R.pick<uint32_t>(colors);
                ps.spawn(R, ParticleKind::Mote, x, y, vx, vy, life, color, 1);
            }
        }
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void speedLines(Paint& p, double k, int direction) const {
        if (k <= .02) return;
        const auto f = jsUint(std::floor(t * 30));
        for (uint32_t i = 0; i < 18; ++i) {
            if (rand({f, i, 3}) > k) continue;
            const double y = jsRound((rand({f, i, 1}) - .5) * 60), L = 16 + rand({f, i, 2}) * 50,
                         x = (rand({f, i, 4}) - .5) * 190, x0 = std::max(-100., x - L / 2),
                         x1 = std::min(100., x + L / 2);
            p.lineFn(
                x0, y, x1, y,
                [&](double u) {
                    const double v = direction > 0 ? u : 1 - u;
                    return v > .75 ? WHITE : v > .35 ? C[6] : C[4];
                },
                [&](double u) { return (direction > 0 ? u : 1 - u) < .25 ? .6 : 1.; });
        }
    }
    ui::WordItems layout(double dx, double stretch, double pulse, double flash, double alpha) const {
        const double  bs = BS * (1 + .12 * pulse), bx = X0 + dx - (big.width * BS * (stretch - 1)) / 2,
                      by = BASE - 7 * bs;
        ui::WordItems items;
        for (const auto& L : big.letters)
            if (L.glyph.width)
                items.add({L.glyph, L.height, bx + L.x * BS * stretch, by, BS * stretch, bs, alpha, flash, 1});
        const double lx = X0 + dx + big.width * BS * stretch + 4;
        for (const auto& L : label.letters)
            if (L.glyph.width)
                items.add(
                    {L.glyph, L.height, lx + L.x * LS * stretch, BASE - 7 * LS, LS * stretch, LS, alpha, flash, .5});
        return items;
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto shake = ui::shake(t, 3, HIT, .25, seed);
        speedLines(p,
                   t < HIT   ? 1
                   : t < OUT ? std::max(0., 1 - (t - HIT) / .35) * .9
                             : seg(t, OUT + .04, OUT + .12) * (1 - seg(t, OUT + .18, OUT + .3)),
                   t >= OUT ? -1 : 1);
        const double hk = t - HIT;
        if (hk >= 0 && hk < .07) {
            const double cx = X0 + (big.width * BS) / 2, k = hk < .035 ? 1 : 1.3;
            for (const auto& s : spikes) {
                const double ca = std::cos(s.angle) * 1.4, sa = std::sin(s.angle);
                if (k == 1)
                    p.beam(cx, 0, cx + ca * s.length, sa * s.length, 3,
                           [&](double u) { return u < .5 ? WHITE : C[6]; });
                else
                    p.line(cx + ca * s.length * .5, sa * s.length * .5, cx + ca * s.length * k, sa * s.length * k,
                           C[5]);
            }
        }
        const double rk = (t - HIT) / .32;
        if (rk > 0 && rk < 1) {
            const double r = 20 + 34 * ease::outCubic(rk);
            p.ellipse(shake.x, shake.y, r * 1.5, r * .75, rk < .3 ? WHITE : C[5],
                      [&](double d) { return d > 1 - 1.5 / (r * .75) ? 1.25 - rk : 0.; });
        }
        ps.draw(p);
        const double x       = xAt(t);
        const bool   moving  = t < HIT || t > OUT + .06;
        const double stretch = moving ? (t < HIT ? 1.22 : 1.12) : 1, pulse = std::exp(-std::pow((t - BEAT) / .05, 2)),
                     flash = (t >= HIT && t < HIT + .07) || pulse > .6 ? 1 : 0,
                     alpha = t > OUT    ? 1 - seg(t, OUT + .08, OUT + .2)
                             : t < .035 ? .5
                                        : 1;
        if (moving && alpha > 0)
            for (const auto& v : std::array<std::pair<double, uint32_t>, 2>{{{.03, C[2]}, {.015, C[3]}}}) {
                auto items = layout(xAt(t - (t < HIT ? v.first : -v.first)) + shake.x, stretch, pulse, 0, alpha);
                const std::array<uint32_t, 1> fill{v.second};
                ui::WordStyle                 style{fill};
                style.edge  = v.second;
                style.under = v.second;
                scratch.draw(p, items.view(), style);
            }
        if (alpha > 0) {
            const double sh    = seg(t, .62, .86);
            auto         items = layout(x + shake.x, stretch, pulse, flash, alpha);
            for (size_t i = 0; i < items.count; ++i) items.data[i].y += shake.y;
            const std::array<uint32_t, 4> fill{C[4], C[5], C[6], C[7]};
            ui::WordStyle                 style{fill};
            style.edge         = WHITE;
            style.under        = C[3];
            style.outline      = C[0];
            style.outlineWidth = 2;
            style.shadow       = shadow;
            style.shine        = sh > 0 && sh < 1 ? std::optional<double>(X0 - 10 + (W + 24) * sh) : std::nullopt;
            style.shineY       = BASE - 28;
            style.glint        = C[6];
            scratch.draw(p, items.view(), style);
            const double bk = seg(t, HIT + .1, HIT + .2) * (t > OUT ? 0 : 1);
            if (bk > 0) {
                const double bw = W * bk, left = 1 - seg(t, HIT + .2, OUT), bx = X0 + x + shake.x,
                             by = BASE + 5 + shake.y;
                p.rect(bx - 1, by - 1, bw + 2, 4, C[0]);
                p.rect(bx, by, bw, 2, C[1]);
                if (left > 0) {
                    p.rect(bx, by, bw * left, 2, C[5]);
                    p.rect(bx, by, bw * left, 1, C[6]);
                    p.px(bx + bw * left - 1, by, WHITE);
                }
            }
        }
        p.glow(shake.x - 20, 0, 60, C[4],
               .2 * alpha + (t >= HIT && t < HIT + .15 ? .5 * (1 - (t - HIT) / .15) : 0) + .25 * pulse);
        const double g = (t - .86) / .22;
        if (g > 0 && g < 1) p.sparkle(X0 + W - 2, BASE - 14, 6 * std::sin(g * PI), C[7], C[5], .5);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& comboCounterDescriptor() {
    static const std::array<Option, 2> options{{{"label", 0, 64, false, 0, &PixelFxUiSettings::label},
                                                {"count", -999999, 999999, false, 0, &PixelFxUiSettings::count}}};
    static const Descriptor            d{
        {PixelFxKind::ComboCounter, "ui/combo-counter", 208, 100, 104, 50, 1.45, false},
        8,
        [](PixelFxSettings& s) {
            s.seed     = 6;
            s.ui.label = "COMBO";
            s.ui.count = 5;
            s.palette  = {hex(0x1a0516), hex(0x3d0a33), hex(0x701560), hex(0xb0228f),
                          hex(0xf53cd1), hex(0xff86e6), hex(0xffd2f6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ComboCounter>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
