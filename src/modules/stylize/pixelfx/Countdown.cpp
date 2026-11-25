// eve-hot-path
// Native adaptation of pixelfx ui/countdown.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double STEP = .62, RADIUS = 30, TOP = -PI / 2, END = 3.1 - .05;
struct CountdownRay {
    double angle, length;
};
struct Countdown final : Kernel {
    const std::vector<uint32_t>  C, G;
    Rng                          rng;
    const uint32_t               seed;
    const int                    n;
    const double                 goTime, outTime;
    const ui::Word               word;
    std::vector<ui::Word>        digits;
    mutable ui::WordScratch      scratch;
    std::array<CountdownRay, 18> rays;
    Particles                    ps, gs;
    Lights                       lights;
    double                       t    = 0;
    int                          beat = -1;
    bool                         went = false, popped = false;
    explicit Countdown(const PixelFxSettings& o)
        : C(o.palette),
          G(o.ui.go),
          rng(o.seed),
          seed(o.seed),
          n(int(clamp(jsRound(o.ui.countdownFrom), 1., 3.))),
          goTime(n * STEP),
          outTime(goTime + .78),
          word(ui::spell(o.ui.word)),
          scratch(size_t((std::max(word.width, 5) + 8) * 10 + 64), 128) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        gs.sparks = {G[7], G[6], G[5], G[3]};
        digits.reserve(size_t(n));
        for (int i = 0; i < n; ++i) digits.push_back(ui::spell(std::to_string(n - i)));
        for (size_t i = 0; i < rays.size(); ++i) {
            const double a      = (double(i) / 18) * TAU + rng.range(-.06, .06),
                         length = i % 2 ? rng.range(10, 16) : rng.range(22, 30);
            rays[i]             = {a, length};
        }
    }
    static double sizeAt(double u, double base, double big) {
        return base + big * std::exp(-u * 26) * std::cos(u * 38);
    }
    static double reach(double v, double d) { return v <= d * 2.2 ? 9 : -std::log(1 - (d * 2.2) / v) / 2.2; }
    void          update(double dt) override {
        t += dt;
        const int i = int(std::floor(t / STEP));
        if (t < goTime && i != beat) {
            beat = i;
            lights.add(0, 0, 54, C[5], .6, .35);
            for (int j = 0; j < 14; ++j) {
                const double a = (double(j) / 14) * TAU + rng.range(-.1, .1), speed = rng.range(90, 200),
                             life = std::min(rng.range(.12, .3), reach(speed, 26));
                ps.spawn(rng, ParticleKind::Spark, std::cos(a) * (RADIUS + 2), std::sin(a) * (RADIUS + 2),
                         std::cos(a) * speed, std::sin(a) * speed, life);
            }
        }
        if (t < goTime) {
            const double k = std::fmod(t, STEP) / STEP, f = 1 - seg(k, .06, .97), head = TOP + TAU * (1 - f);
            if (f > .01 && f < .99 && rng.isChance(.7)) {
                const double hx = std::cos(head) * RADIUS, hy = std::sin(head) * RADIUS, tx = -std::sin(head),
                             ty = std::cos(head), out = rng.range(-.4, 1), speed = rng.range(60, 150),
                             life = rng.range(.12, .28);
                ps.spawn(rng, ParticleKind::Spark, hx, hy, (-tx * .8 + std::cos(head) * out) * speed,
                         (-ty * .8 + std::sin(head) * out) * speed - 20, life);
                if (rng.isChance(.15)) {
                    const double vx = std::cos(head) * rng.range(5, 20), vy = std::sin(head) * rng.range(5, 20),
                                 emberLife = rng.range(.4, .8);
                    ps.spawn(rng, ParticleKind::Ember, hx, hy, vx, vy, emberLife, C[5], 1);
                }
            }
        }
        if (!went && t >= goTime) {
            went = true;
            lights.add(0, 0, 70, WHITE, .9, .18);
            lights.add(0, 0, 64, G[5], .7, .8);
            for (int j = 0; j < 36; ++j) {
                const double a = rng.range(0, TAU), speed = rng.range(120, 300),
                             life = std::min(rng.range(.18, .36), reach(speed, 24));
                gs.spawn(rng, ParticleKind::Spark, std::cos(a) * RADIUS, std::sin(a) * RADIUS, std::cos(a) * speed,
                         std::sin(a) * speed - 40, life);
            }
            const std::array<uint32_t, 3> colors{G[7], G[6], G[5]};
            for (int j = 0; j < 18; ++j) {
                const double a = rng.range(0, TAU), speed = rng.range(30, 80), r = rng.range(10, 28),
                             life  = rng.range(.5, 1);
                const auto   color = rng.pick<uint32_t>(colors);
                const bool   big   = rng.isChance(.4);
                const double size  = big ? rng.range(1.5, 2.5) : .6;
                gs.spawn(rng, ParticleKind::Star, std::cos(a) * r, std::sin(a) * r, std::cos(a) * speed,
                         std::sin(a) * speed - 10, life, color, size);
            }
        }
        if (t > goTime + .2 && t < outTime - .1 && rng.isChance(.12)) {
            const double a = rng.range(0, TAU), r = rng.range(30, 46), vy = -rng.range(4, 12), life = rng.range(.3, .6);
            const std::array<uint32_t, 2> colors{G[7], G[6]};
            const auto                    color = rng.pick<uint32_t>(colors);
            const double                  size  = rng.isChance(.3) ? 1.6 : .6;
            gs.spawn(rng, ParticleKind::Star, std::cos(a) * r, std::sin(a) * r * .8, 0, vy, life, color, size);
        }
        if (!popped && t >= outTime + .05) {
            popped = true;
            lights.add(0, 0, 40, G[6], .6, .25);
            const std::array<uint32_t, 3> colors{G[7], G[6], G[5]};
            for (int j = 0; j < 14; ++j) {
                const double a = (double(j) / 14) * TAU + rng.range(-.15, .15), speed = rng.range(60, 130),
                             life  = std::min(rng.range(.3, .45), END - t);
                const auto   color = rng.pick<uint32_t>(colors);
                const double size  = j % 3 ? .6 : rng.range(1.3, 2);
                gs.spawn(rng, ParticleKind::Star, std::cos(a) * 12, std::sin(a) * 8, std::cos(a) * speed,
                         std::sin(a) * speed * .8 - 10, life, color, size);
            }
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        gs.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void timer(Paint& p, double x, double y, double f, bool flash) const {
        p.ring(x, y, RADIUS + 4, 8, [&](double v) { return v < .13 || v > .87 ? C[0] : C[1]; });
        for (int j = 0; j < 40; ++j) {
            const double a = (double(j) / 40) * TAU + t * .6;
            p.dot(x + std::cos(a) * (RADIUS - 6), y + std::sin(a) * (RADIUS - 6), j % 5 ? C[2] : C[4], 1);
        }
        const double head = TOP + TAU * (1 - f);
        for (int i = 0; i < 12; ++i) {
            const double a   = TOP + (i * TAU) / 12;
            const bool   lit = a >= head - 1e-6 || f >= .999, near = std::abs(a - head) < .2 && f < .999;
            const double ca = std::cos(a), sa = std::sin(a);
            p.line(x + ca * (RADIUS + 6), y + sa * (RADIUS + 6), x + ca * (RADIUS + 8), y + sa * (RADIUS + 8),
                   flash || near ? WHITE
                   : lit         ? C[5]
                                 : C[2]);
        }
        if (f <= .002) return;
        p.arc(x, y, RADIUS + 2, 5., head, TOP + TAU, [&](double u, double v) {
            const double d = u * TAU * f;
            if (flash || d < .1) return WHITE;
            if (d < .32) return v > .5 ? WHITE : C[6];
            return v > .75 ? C[6] : v > .4 ? C[5] : v > .15 ? C[4] : C[3];
        });
        for (double a = head + .25; a < TOP + TAU; a += .55)
            p.glow(x + std::cos(a) * (RADIUS - .5), y + std::sin(a) * (RADIUS - .5), 9, C[4], .12);
        const double hx = x + std::cos(head) * (RADIUS - .5), hy = y + std::sin(head) * (RADIUS - .5);
        p.glow(hx, hy, 14, C[5], .5);
        if (!flash) p.sparkle(hx, hy, 2 + 1.5 * (.5 + .5 * std::sin(t * 40)), C[6], C[5], .4);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        if (t < goTime) {
            const int    i = int(std::floor(t / STEP));
            const double u = t - i * STEP, k = u / STEP;
            const auto   offset = ui::shake(t, 3, i * STEP, .22, seed + uint32_t(i));
            const double f      = 1 - seg(k, .06, .97);
            const bool   flash  = u < .07;
            const double rk     = u / .3;
            if (rk < 1)
                p.ring(offset.x, offset.y, RADIUS + 6 + 22 * ease::outCubic(rk), 1.5 * (1 - rk) + 1,
                       rk < .3 ? WHITE : C[5], 1.3 * (1 - rk));
            timer(p, offset.x, offset.y, f, flash);
            if (u < .035)
                p.circle(offset.x, offset.y, RADIUS - 3, WHITE);
            else if (u < .07)
                p.ring(offset.x, offset.y, RADIUS - 3, 3, C[6]);
            const double  s = sizeAt(u, 5, 2);
            const auto&   d = digits[size_t(i)];
            const double  W = d.width * s, H = 7 * s, x0 = offset.x - W / 2, y0 = offset.y - H / 2;
            ui::WordItems items;
            for (const auto& L : d.letters)
                if (L.glyph.width)
                    items.add({L.glyph, L.height, x0 + L.x * s, y0, s, s, 1, u < .035 ? 1. : u < .07 ? .4 : 0.});
            const std::array<uint32_t, 4> fill{C[4], C[5], C[6], C[7]};
            ui::WordStyle                 style{fill};
            style.edge         = WHITE;
            style.under        = C[3];
            style.outline      = C[0];
            style.outlineWidth = 2;
            style.shadow       = scale(C[0], .5);
            scratch.draw(p, items.view(), style);
            p.glow(offset.x, offset.y, 30, C[4], .22 + (u < .12 ? .6 * (1 - u / .12) : 0));
            p.glow(offset.x, offset.y, RADIUS + 8, C[3], .2 * (1 - k) + .08);
        } else {
            const double u      = t - goTime;
            const auto   offset = ui::shake(t, 4, goTime, .3, seed + 9);
            const double bk     = u / .4;
            if (bk < 1) {
                const double r = RADIUS + 4 + 30 * ease::outCubic(bk), w = std::max(1., 7 * (1 - bk));
                p.ring(0, 0, r, w, [&](double v) { return bk < .2 || v > .6 ? WHITE : G[6]; }, 1.4 * (1 - bk));
                p.glowRing(0, 0, r - w / 2, 8, G[5], .5 * (1 - bk));
            }
            const double ik = (u - .05) / .3;
            if (ik > 0 && ik < 1) p.ring(0, 0, RADIUS + 22 * ease::outCubic(ik), 1, G[5], 1.2 * (1 - ik));
            const double rk = ease::outExpo(clamp(u / .18)) * (1 - ease::inQuad(seg(t, outTime - .25, outTime + .05)));
            for (const auto& ray : rays) {
                if (rk <= .02) break;
                const double a = ray.angle + u * .35, ca = std::cos(a), sa = std::sin(a),
                             r0 = 1 / std::sqrt(std::pow(ca / 40, 2) + std::pow(sa / 23, 2)) + 3 * rk,
                             r1 = r0 + ray.length * .7 * rk;
                p.lineFn(
                    ca * r0, sa * r0, ca * r1, sa * r1,
                    [&](double q) { return q < .3    ? WHITE
                                           : q < .65 ? G[6]
                                                     : G[5]; },
                    [](double q) { return q < .6 ? 1.3 : (1.3 * (1 - q)) / .4; });
            }
            const double pu = (t - outTime) / .06;
            if (pu < 1) {
                double       s = sizeAt(u, 5, 3.5), flash = u < .035 ? 1 : u < .07 ? .4 : 0;
                const double throb = std::max(0., std::sin((u - .25) * 9)) * smooth(.25, .35, u);
                if (pu > 0) {
                    s     = 5 * (1 + .25 * pu);
                    flash = 1;
                }
                const double  W = word.width * s, H = 7 * s, x0 = offset.x - W / 2, y0 = offset.y - H / 2;
                ui::WordItems items;
                for (const auto& L : word.letters)
                    if (L.glyph.width) items.add({L.glyph, L.height, x0 + L.x * s, y0, s, s, 1, flash, 1});
                const double sh = seg(u, .32, .56);
                const auto   shine =
                    sh > 0 && sh < 1 ? std::optional<double>(x0 - 8 + (W + 22) * ease::inOutSine(sh)) : std::nullopt;
                const std::array<uint32_t, 4> fill{G[4], G[5], G[6], G[7]};
                ui::WordStyle                 style{fill};
                style.edge         = WHITE;
                style.under        = G[3];
                style.outline      = G[0];
                style.outlineWidth = 2;
                style.shadow       = scale(G[0], .5);
                style.shine        = shine;
                style.shineY       = y0;
                style.glint        = G[6];
                scratch.draw(p, items.view(), style);
                p.glow(offset.x, offset.y, 46, G[5], .25 + .15 * throb + (u < .12 ? .7 * (1 - u / .12) : 0));
            } else {
                const double k = (t - outTime - .06) / .22;
                if (k > 0 && k < 1) p.ring(0, 0, 8 + 26 * ease::outCubic(k), 1, k < .4 ? WHITE : G[6], 1.3 * (1 - k));
            }
        }
        ps.draw(p);
        gs.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& countdownDescriptor() {
    static const std::array<Option, 3> options{{{"from", 1, 3, false, 0, &PixelFxUiSettings::countdownFrom},
                                                {"word", 0, 64, false, 0, &PixelFxUiSettings::word},
                                                {"go", 1, 64, false, 8, &PixelFxUiSettings::go}}};
    static const Descriptor            d{
        {PixelFxKind::Countdown, "ui/countdown", 152, 136, 76, 68, 3.1, false},
        8,
        [](PixelFxSettings& s) {
            s.seed             = 5;
            s.ui.countdownFrom = 3;
            s.ui.word          = "GO!";
            s.palette          = {hex(0x0a1030), hex(0x142a66), hex(0x1f4fae), hex(0x2f86e6),
                                  hex(0x5cc4ff), hex(0xa8e8ff), hex(0xe6fbff), WHITE};
            s.ui.go            = {hex(0x0b2410), hex(0x16501c), hex(0x22882a), hex(0x3cc23a),
                                  hex(0x7ee84c), hex(0xc4ff8a), hex(0xf0ffd6), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<Countdown>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
