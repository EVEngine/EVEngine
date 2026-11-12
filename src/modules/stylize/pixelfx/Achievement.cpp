// eve-hot-path
// Native adaptation of pixelfx ui/achievement.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiHud.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double PW = 130, PH = 34, PX = -65, PY = -17, IN = .2, MEDAL = .26, SHINE = .95, OUT = 1.92, MX = -48, MY = 2;
const ui::PanelColors INK{hex(0x0d0a14), hex(0x5a4f74), hex(0x2a2338), hex(0x211b2d), hex(0x15111c)};
struct Achievement final : Kernel {
    const std::vector<uint32_t> G, B;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    const ui::Word              title, sub;
    mutable ui::WordScratch     scratch;
    double                      t      = 0;
    bool                        popped = false;
    explicit Achievement(const PixelFxSettings& o)
        : G(o.palette),
          B(o.ui.ribbon),
          rng(o.seed),
          title(ui::spell(o.ui.title, PixelFont::Mini)),
          sub(ui::spell(o.ui.text, PixelFont::Mini)),
          scratch(size_t((std::max(title.width, sub.width) + 8) * 2 + 32), 64) {
        ps.sparks = {G[7], G[6], G[5], G[3]};
    }
    static double xAt(double time) {
        return time < IN                       ? 30 * std::pow(1 - time / IN, 3)
               : time < OUT                    ? -3 * std::exp(-(time - IN) * 12) * std::sin((time - IN) * 30)
               : seg(time, OUT, OUT + .06) < 1 ? -3 * seg(time, OUT, OUT + .06)
                                               : -3 + 33 * ease::inQuad(seg(time, OUT + .06, OUT + .24));
    }
    void update(double dt) override {
        t += dt;
        if (!popped && t >= MEDAL + .06) {
            popped = true;
            lights.add(MX, MY, 36, G[5], .7, .4);
            const std::array<uint32_t, 3> colors{G[7], G[6], G[5]};
            for (int i = 0; i < 12; ++i) {
                const double a = (double(i) / 12) * TAU + rng.range(-.2, .2), speed = rng.range(40, 90),
                             life  = rng.range(.25, .45);
                const auto   color = rng.pick<uint32_t>(colors);
                const double size  = i % 3 ? .6 : rng.range(1.2, 1.8);
                ps.spawn(rng, ParticleKind::Star, MX + std::cos(a) * 9, MY + std::sin(a) * 9, std::cos(a) * speed,
                         std::sin(a) * speed * .6, life, color, size);
            }
        }
        if (t > .5 && t < OUT && rng.isChance(.05)) {
            const double x = MX + rng.range(-11, 11), y = MY + rng.range(-11, 9), vy = -rng.range(4, 10),
                         life = std::min(rng.range(.3, .5), 2.4 - .05 - t);
            const double size = rng.isChance(.3) ? 1.4 : .6;
            ps.spawn(rng, ParticleKind::Star, x, y, 0, vy, life, G[6], size);
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void medal(Paint& p, double x, double y, double s, bool white, std::optional<double> shine) const {
        const double r = 10 * s;
        for (double sd : {-1., 1.}) {
            const std::array<Point, 4> strap{{{x + sd * 2 * s, y - r + 2},
                                              {x + sd * 7 * s, y - r + 2},
                                              {x + sd * 9 * s, PY + 1},
                                              {x + sd * 4 * s, PY + 1}}};
            p.poly(strap, [&](double px) { return std::abs(px - x) < 4.5 * s ? B[2] : B[3]; });
            p.line(x + sd * 2 * s, y - r + 2, x + sd * 4 * s, PY + 1, B[0]);
            p.line(x + sd * 7 * s, y - r + 2, x + sd * 9 * s, PY + 1, B[0]);
        }
        std::array<Point, 10> points;
        for (size_t i = 0; i < points.size(); ++i) {
            const double a = -PI / 2 + double(i) * PI / 5, radius = i % 2 ? 2.8 * s : 6.5 * s;
            points[i] = {x + std::cos(a) * radius, y + .5 + std::sin(a) * radius};
        }
        const auto inside = [&](double px, double py) {
            bool in = false;
            for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                const auto& a = points[i];
                const auto& b = points[j];
                if ((a.y > py) != (b.y > py) && px < ((b.x - a.x) * (py - a.y)) / (b.y - a.y) + a.x) in = !in;
            }
            return in;
        };
        p.shader(x - r - 1, y - r - 1, x + r + 1, y + r + 1, [&](double px, double py, ShadeOutput& out) {
            const double dx = px - x, dy = py - y, d = jsHypot2(dx, dy);
            if (d > r) return;
            if (white) {
                out.c = r - d < 1 ? G[6] : WHITE;
                return;
            }
            if (r - d < 1) {
                out.c = G[0];
                return;
            }
            const double l = -(dx * .6 + dy * .8) / r;
            if (shine && std::abs(dx - dy * .6 - *shine) < 1.2) {
                out.c = WHITE;
                return;
            }
            if (r - d < 2.5) {
                out.c = l > .2 ? G[6] : l > -.3 ? G[4] : G[3];
                return;
            }
            if (inside(px, py)) {
                out.c = !inside(px + 1, py) || !inside(px - 1, py) || !inside(px, py + 1) || !inside(px, py - 1) ? G[2]
                        : dx + dy < 0                                                                            ? G[7]
                                                                                                                 : G[6];
                return;
            }
            out.c = l > .4 ? G[5] : l > -.2 ? G[4] : G[3];
        });
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double x     = xAt(t);
        const bool   out   = t > OUT + .06;
        const double alpha = t < .05 ? .5 : out ? 1 - seg(t, OUT + .12, OUT + .24) : 1;
        if (alpha <= 0) return;
        if (t < IN * .8 || out)
            for (uint32_t i = 0; i < 8; ++i) {
                const auto   tick = jsUint(std::floor(t * 30));
                const double y = PY + 3 + rand({tick, i, 1}) * (PH - 6), L = 10 + rand({tick, i, 2}) * 26,
                             x0 = x + PW / 2 + 2 + rand({i, 3}) * 6;
                p.lineFn(
                    x0, y, std::min(106., x0 + L), y, [&](double u) { return u < .3   ? WHITE
                                                                             : u < .6 ? G[6]
                                                                                      : G[4]; },
                    [&](double u) { return alpha * (1.1 - u); });
            }
        const double sh = seg(t, SHINE, SHINE + .36);
        const auto   shine =
            sh > 0 && sh < 1 ? std::optional<double>(PX - 12 + (PW + 30) * ease::inOutSine(sh)) : std::nullopt;
        const auto placedShine = shine ? std::optional<double>(*shine + x) : std::nullopt;
        ui::panel(p, PX + x, PY, PW, PH, INK, 4, alpha, placedShine);
        p.line(PX + x + 4, PY + 1, PX + x + PW - 5, PY + 1, G[4], alpha);
        p.px(PX + x + 3, PY + 1, G[2]);
        ui::WordItems items;
        for (const auto& L : title.letters)
            if (L.glyph.width) items.add({L.glyph, L.height, PX + 34 + x + L.x * 2., PY + 5, 2, 2, alpha});
        const std::array<uint32_t, 3> titleFill{G[4], G[5], G[6]};
        ui::WordStyle                 titleStyle{titleFill};
        titleStyle.edge    = G[7];
        titleStyle.under   = G[3];
        titleStyle.outline = INK.outline;
        titleStyle.shine   = placedShine;
        titleStyle.shineY  = PY;
        titleStyle.glint   = G[7];
        scratch.draw(p, items.view(), titleStyle);
        items.count = 0;
        for (const auto& L : sub.letters)
            if (L.glyph.width) items.add({L.glyph, L.height, PX + 34 + x + L.x * 2., PY + 19, 2, 2, alpha});
        const std::array<uint32_t, 3> subFill{hex(0x9a94b0), hex(0xd8d4e6), WHITE};
        ui::WordStyle                 subStyle{subFill};
        subStyle.edge    = WHITE;
        subStyle.under   = hex(0x6e6886);
        subStyle.outline = INK.outline;
        subStyle.shine   = placedShine;
        subStyle.shineY  = PY;
        scratch.draw(p, items.view(), subStyle);
        const double mk = t - MEDAL;
        if (mk > 0) {
            const double s = mk < .035 ? 1.5 : 1 + .35 * std::exp(-mk * 16) * std::cos(mk * 28);
            medal(p, MX + x, MY, s, mk < .07, shine ? std::optional<double>(*shine - MX + 4) : std::nullopt);
            p.glow(MX + x, MY, 20, G[5], .3 * alpha + (mk < .15 ? .5 * (1 - mk / .15) : 0));
            const double g = std::fmod(t - 1.5, .9) / .9;
            if (t > 1.5 && g < .25) p.sparkle(MX + x - 4, MY - 4, 4 * std::sin((g / .25) * PI), G[7], G[6], .5);
        }
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& achievementDescriptor() {
    static const std::array<Option, 3> options{{{"ribbon", 1, 64, false, 5, &PixelFxUiSettings::ribbon},
                                                {"title", 0, 64, false, 0, &PixelFxUiSettings::title},
                                                {"text", 0, 64, false, 0, &PixelFxUiSettings::text}}};
    static const Descriptor            d{
        {PixelFxKind::Achievement, "ui/achievement", 220, 72, 110, 36, 2.4, false},
        8,
        [](PixelFxSettings& s) {
            s.seed      = 9;
            s.ui.title  = "ACHIEVEMENT";
            s.ui.text   = "UNLOCKED";
            s.palette   = {hex(0x2a1405), hex(0x5c3008), hex(0x9a5a0e), hex(0xd48e18),
                           hex(0xf5b92c), hex(0xffde5e), hex(0xfff4bc), WHITE};
            s.ui.ribbon = {hex(0x0c1430), hex(0x1f3a7a), hex(0x2f66c4), hex(0x5aa0f0), hex(0xa6d4ff)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Achievement>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
