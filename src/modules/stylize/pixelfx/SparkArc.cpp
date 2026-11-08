// eve-hot-path
// Native adaptation of pixelfx lightning/spark-arc.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see NOTICE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Lightning.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SNAP = 1.42, H = 58, P = 2;
Point            rodAt(double s, double h) { return {s * (4.5 + (h / 60) * 19.5), -9 - h}; }
double           height(double t) { return 1.5 + H * std::pow(clamp(t / SNAP), 1.35); }
void             draw(Paint& p, double t, const PixelFxSettings& o) {
    const auto&                   C = o.palette;
    const auto&                   S = o.lightning.sparks;
    const auto                    f = jsUint(wrap(std::floor(t * 30 + 1e-6), 60));
    Rng                           r(hash({o.seed, 1, f}));
    const bool                    climbing = t < SNAP;
    const double                  h = height(t), u = clamp(t / SNAP), a = t - SNAP;
    const std::array<uint32_t, 6> STEEL{
        {hex(0x151823), hex(0x262c3b), hex(0x3d4558), hex(0x5f6a82), hex(0x8f9bb2), hex(0xd2daea)}};
    const std::array<uint32_t, 4> CERAMIC{{hex(0x4b4458), hex(0x857d96), hex(0xc9c1d6), hex(0xefe9f6)}};
    p.glow(0, -14, 50, C[3], climbing ? .2 + .15 * (1 - u) : .06);
    p.rect(-18, -3, 36, 3, STEEL[1]);
    p.line(-18, -3, 17, -3, STEEL[3]);
    for (int s : {-1, 1})
        for (int y = -9; y < -3; ++y) {
            const bool rib = (y & 1) == 0;
            for (int x = 2; x <= 6; ++x)
                p.px(s * x - (s < 0 ? 1 : 0), y, rib ? CERAMIC[x < 4 ? 3 : x < 6 ? 2 : 1] : CERAMIC[x < 5 ? 1 : 0]);
        }
    for (int s : {-1, 1}) {
        for (double hh = 0; hh <= 60; hh += .5) {
            const auto   v  = rodAt(s, hh);
            const double tp = SNAP * std::pow(clamp((hh - 1.5) / H), 1 / 1.35), age = t >= tp ? t - tp : t - tp + P,
                         hot = hh > H + 2 ? 0 : std::exp(-age / .26);
            const auto   c   = lightning::heat(hot * 1.05 - .12);
            p.px(v.x, v.y, c ? c : s < 0 ? STEEL[4] : STEEL[3]);
            const auto warm = lightning::heat(hot * .6);
            p.px(v.x + s, v.y, c && hot > .35 ? (warm ? warm : STEEL[2]) : s < 0 ? STEEL[2] : STEEL[1]);
            if (hot > .3 && (jsUint(jsRound(hh * 2)) & 3u) == 0) p.glow(v.x, v.y, 5, c ? c : C[4], .25 * hot);
        }
        const auto v = rodAt(s, 60);
        p.circle(v.x + s * .5, v.y, 1.6, STEEL[4]);
        p.px(v.x + s * .5 - .5, v.y - 1, STEEL[5]);
    }
    if (climbing) {
        const auto        l = rodAt(-1, h), rr = rodAt(1, h);
        const double      k0 = t < 1. / 30 ? 1 : 1 - .22 * u, sput = u > .88 && r.next() < .25 ? .4 : 1,
                          k = k0 * sput * (.82 + .18 * r.next()), bow = -(1.5 + 13 * u * u) * (.8 + .4 * r.next());
        auto              paths = lightning::bowed(r, l.x, l.y, rr.x, rr.y, bow, .18 + .12 * u, .12 + .25 * u);
        lightning::Stroke opts;
        opts.hero  = t < 1. / 30;
        opts.light = .7;
        lightning::stroke(p, paths, k, C, t < 1. / 30 ? .95 : .8, opts);
        p.glowLine(l.x, l.y + bow * .5, rr.x, rr.y + bow * .5, 12, C[4], .22 * k);
        for (auto v : {l, rr}) {
            p.circle(v.x, v.y, 1.8 + .6 * k, C[5]);
            p.px(v.x, v.y, WHITE);
            p.glow(v.x, v.y, 12, C[5], .55 * k);
        }
        if (t < .1) p.glow(0, -11, 26, C[6], .45 * (1 - t / .1));
    } else if (a < 2. / 30) {
        const auto l = rodAt(-1, H + 1.5), rr = rodAt(1, H + 1.5);
        for (int i = 0; i < 2; ++i) {
            const auto   v  = i == 0 ? l : rr;
            const int    s  = i == 0 ? 1 : -1;
            const double x1 = v.x + s * r.range(5, 9), y1 = v.y - r.range(2, 6);
            lightning::crackle(p, lightning::zigzag(r, v.x, v.y, x1, y1, .4, .3), .7, C, .4);
        }
        p.glow(0, l.y - 8, 30, C[4], .5);
    } else if (t > P - .16 && r.next() < .7) {
        const double y0 = -10.5 - r.range(0, 2), y1 = -10.5 - r.range(0, 2);
        auto         paths = lightning::zigzag(r, -4.5, y0, 4.5, y1, .5, 0);
        const double k     = .4 + .3 * r.next();
        lightning::crackle(p, paths, k, C, .35);
    }
    for (int j = 0; j < 3; ++j) {
        const double born = ((j + 1) / 3.2) * SNAP, age = wrap(t - born, P);
        if (age >= 1) continue;
        const double y0 = -9 - height(born) - 6;
        lightning::puff(p, (j % 2 ? 3 : -3) + std::sin(age * 3 + j) * 2, y0 - age * 20, 1.8 + j * .3, age,
                        mix(hex(0x6a6478), C[5], .3));
    }
    const auto burst = [&](double t0, int n, uint32_t salt, int type) {
        const double age = wrap(t - t0, P);
        for (int j = 0; j < n; ++j) {
            Rng          q(hash({o.seed, salt, uint32_t(j)}));
            const double life = q.range(.25, .6);
            if (age >= life) continue;
            Point  v;
            double a0, spread, sp0, sp1;
            if (type == 0) {
                v      = rodAt(j % 2 ? 1 : -1, 1.5);
                a0     = -PI / 2 + (j % 2 ? .6 : -.6);
                spread = 1;
                sp0    = 60;
                sp1    = 170;
            } else if (type == 1) {
                v      = {rodAt(1, H).x * q.range(-.8, .8), -9 - H - 6};
                a0     = -PI / 2;
                spread = 1.3;
                sp0    = 50;
                sp1    = 160;
            } else {
                v      = rodAt(j % 2 ? 1 : -1, height(t0));
                a0     = PI / 2;
                spread = .5;
                sp0    = 5;
                sp1    = 30;
            }
            const double ang = a0 + q.range(-spread, spread), sp = q.range(sp0, sp1);
            lightning::spark(p, lightning::flight(v.x, v.y, std::cos(ang) * sp, std::sin(ang) * sp, age, 0),
                             1 - age / life, S);
        }
    };
    burst(0, 12, 11, 0);
    burst(SNAP, 14, 12, 1);
    burst(.45, 2, 20, 2);
    burst(.8, 2, 21, 2);
    burst(1.15, 2, 22, 2);
    if (climbing || a < .25) p.glow(0, -9 - h - 3, 26, C[4], climbing ? .25 : .4 * (1 - a / .25));
}
}  // namespace
const Descriptor& sparkArcDescriptor() {
    static const std::array<Option, 1> options{{{"sparks", 1, 64, false, 4, &PixelFxLightningSettings::sparks}}};
    static const Descriptor            d{{PixelFxKind::SparkArc, "lightning/spark-arc", 124, 120, 62, 108, 2, true},
                                         8,
                                         [](PixelFxSettings& s) {
                                  s.seed = 2;
                                  s.palette.assign(lightning::VOLT.begin(), lightning::VOLT.end());
                                  s.lightning.sparks.assign(lightning::PLASMA.begin(), lightning::PLASMA.end());
                                         },
                                         draw,
                                         options};
    return d;
}
}  // namespace eve::stylize::pixelfx
