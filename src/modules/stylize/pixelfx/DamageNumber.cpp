// eve-hot-path
// Native adaptation of pixelfx ui/damage-number.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LAND = -8, FADE = .84, DURATION = 1.05;
struct DamageSpike {
    double angle, length;
};
struct DamageNumber final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    const ui::Word              word;
    mutable ui::WordScratch     scratch;
    Particles                   ps;
    Lights                      lights;
    std::array<DamageSpike, 9>  spikes;
    double                      t = 0, x = 0, y = LAND, vx = 34, vy = -175, squash = 0;
    int                         hops = 0;
    bool                        hit  = false;
    explicit DamageNumber(const PixelFxSettings& o)
        : C(o.palette), R(o.seed), word(ui::spell(o.ui.text)), scratch(size_t((word.width + 8) * 8 + 64), 128) {
        ps.sparks = {C[7], C[6], C[4], C[2]};
        for (size_t i = 0; i < spikes.size(); ++i) {
            const double a = (double(i) / 9) * TAU + R.range(-.2, .2), len = R.range(8, 14);
            spikes[i] = {a, len};
        }
    }
    void update(double dt) override {
        t += dt;
        if (!hit) {
            hit = true;
            lights.add(0, -4, 30, C[5], .7, .25);
            lights.add(0, -4, 14, WHITE, .6, .08);
            for (int i = 0; i < 9; ++i) {
                const double a = R.range(-PI * .95, -.05) + (i % 2 ? PI * .08 : 0), speed = R.range(90, 200),
                             life = R.range(.1, .25);
                ps.spawn(R, ParticleKind::Spark, 0, -4, std::cos(a) * speed, std::sin(a) * speed, life);
            }
        }
        if (hops < 2) {
            vy += 900 * dt;
            vx -= vx * 2 * dt;
            x += vx * dt;
            y += vy * dt;
            if (y >= LAND && vy > 0) {
                y      = LAND;
                squash = 1;
                ++hops;
                vy = hops < 2 ? -vy * .36 : 0;
                if (hops == 1)
                    for (double side : {-1., 1.}) {
                        const double py = LAND + R.range(-2, 1), pvx = side * R.range(15, 35), pvy = -R.range(5, 20),
                                     life = R.range(.2, .35), size = R.range(.5, 1.2);
                        ps.spawn(R, ParticleKind::Star, x + side * 14, py, pvx, pvy, life, C[6], size);
                    }
            }
        }
        squash = std::max(0., squash - dt / .09);
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        if (t < .07) {
            const bool big = t < .035;
            for (size_t i = 0; i < spikes.size(); ++i) {
                const auto&  spike = spikes[i];
                const double l = spike.length * (big ? 1 : 1.3), ca = std::cos(spike.angle), sa = std::sin(spike.angle);
                if (big) {
                    p.line(-sa, -4 + ca, ca * l * .7 - sa, -4 + sa * l * .7 + ca, C[4]);
                    p.line(sa, -4 - ca, ca * l * .7 + sa, -4 + sa * l * .7 - ca, C[4]);
                }
                p.line(ca * (big ? 0 : l * .45), -4 + sa * (big ? 0 : l * .45), ca * l, -4 + sa * l,
                       big     ? WHITE
                       : i % 2 ? C[6]
                               : C[5]);
            }
            if (big) p.circle(0, -4, 3.2, WHITE);
        } else if (t < .16)
            p.sparkle(0, -4, 5 * (1 - (t - .07) / .09), WHITE, C[5], .6);
        const double rk = t / .22;
        if (rk < 1)
            p.ring(0, -4, 3 + 10 * ease::outCubic(rk), std::max(1., 2 * (1 - rk)), rk < .4 ? WHITE : C[5], 1.2 - rk);
        ps.draw(p);
        const double out = seg(t, FADE, DURATION - .04);
        if (out < 1) {
            const double s  = 2 + 1.4 * std::exp(-t * 22) * std::cos(t * 30);
            double       sx = s, sy = s;
            if (squash > 0) {
                sx = s * (1 + .18 * squash);
                sy = s * (1 - .2 * squash);
            } else if (hops < 2 && std::abs(vy) > 120) {
                sx = s * .92;
                sy = s * 1.1;
            }
            const double  W = word.width * sx, H = 7 * sy, x0 = x - W / 2, y0 = y - H - 6 * ease::inQuad(out),
                          flash = t < .035  ? 1
                                  : t < .07 ? .4
                                            : 0;
            ui::WordItems items;
            for (const auto& letter : word.letters)
                if (letter.glyph.width)
                    items.add({letter.glyph, letter.height, x0 + letter.x * sx, y0, sx, sy, 1 - out, flash});
            const std::array<uint32_t, 4> fill{C[3], C[3], C[4], C[5]};
            ui::WordStyle                 style{fill};
            style.edge    = C[6];
            style.under   = C[2];
            style.outline = C[0];
            style.shadow  = scale(C[0], .5);
            scratch.draw(p, items.view(), style);
            p.glow(x, y0 + H / 2, 18, C[4], .3 * (1 - out) + (t < .1 ? .4 * (1 - t / .1) : 0));
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& damageNumberDescriptor() {
    static const std::array<Option, 1> options{{{"text", 0, 64, false, 0, &PixelFxUiSettings::text}}};
    static const Descriptor            d{
        {PixelFxKind::DamageNumber, "ui/damage-number", 76, 68, 30, 52, 1.05, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 2;
            s.ui.text = "-27";
            s.palette = {hex(0x1a0508), hex(0x4a0b12), hex(0x8c1420), hex(0xd42a2e),
                         hex(0xff5a4a), hex(0xff9a86), hex(0xffd8cc), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<DamageNumber>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
