// eve-hot-path
// Native adaptation of pixelfx melee/sword-slash.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct SwordArc {
    double radius, width, a0, a1, t0, sweep, hold, trail;
};
struct SwordSlash final : Kernel {
    const std::vector<uint32_t> C;
    const SwordArc              main, echo;
    const melee::Travel         tip;
    Rng                         R;
    Particles                   ps;
    double                      t      = 0;
    bool                        landed = false;
    static Point                onArc(const SwordArc& s, double r, double angle) {
        const double lx = std::cos(angle) * r, ly = std::sin(angle) * r * .6, c = std::cos(-.38), n = std::sin(-.38);
        return {6 + lx * c - ly * n, -26 + lx * n + ly * c};
    }
    static melee::Travel tipOf(const SwordArc& s) {
        const double direction = s.a1 - s.a0 > 0 ? 1. : s.a1 - s.a0 < 0 ? -1. : 0.;
        const auto   p         = onArc(s, s.radius - s.width * .3, s.a1),
                     p2        = onArc(s, s.radius - s.width * .3, s.a1 + direction * .05);
        const double norm = jsHypot2(p2.x - p.x, p2.y - p.y), d = norm != 0 ? norm : 1;
        return {p.x, p.y, (p2.x - p.x) / d, (p2.y - p.y) / d, s.a1};
    }
    explicit SwordSlash(const PixelFxSettings& o)
        : C(o.palette),
          main{o.melee.radius, 12, -2.55, .95, 0, .06, .06, .24},
          echo{o.melee.radius * (33. / 42),
               o.melee.radius * (4.5 / 42),
               -2.55 + (.95 - (-2.55)) * .1,
               .95 - (.95 - (-2.55)) * (.15 / 3.5),
               .017,
               .06,
               .03,
               .2},
          tip(tipOf(main)),
          R(o.seed) {
        ps.sparks = o.melee.sparks;
    }
    void update(double dt) override {
        t += dt;
        if (!landed && t >= main.sweep) {
            landed = true;
            for (int i = 0; i < 10; ++i) {
                const double angle = std::atan2(tip.dy, tip.dx) + R.range(-.5, .5), speed = R.range(150, 400),
                             life = R.range(.12, .35);
                ps.spawn(R, ParticleKind::Spark, tip.x, tip.y, std::cos(angle) * speed, std::sin(angle) * speed, life);
            }
            for (int i = 0; i < 16; ++i) {
                const double angle = main.a0 + (main.a1 - main.a0) * R.range(.3, 1);
                const auto   p     = onArc(main, main.radius, angle);
                const double ox = p.x - 6, oy = p.y - (-26), norm = jsHypot2(ox, oy), d = norm != 0 ? norm : 1,
                             speed = R.range(30, 100), life = R.range(.25, .55);
                const auto   color = C[size_t(4 + R.integer(0, 1))];
                ps.spawn(R, ParticleKind::Mote, p.x, p.y, (ox / d) * speed, (oy / d) * speed - 10, life, color, .6);
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
    }
    void crescent(Paint& p, const SwordArc& s, double angle, double length, double width, double age,
                  std::span<const uint32_t> ramp) const {
        if (std::abs(length) < .02 || width < .3) return;
        const int    n         = int(ramp.size());
        const double direction = length > 0   ? 1.
                                 : length < 0 ? -1.
                                              : 0.,
                     span = std::abs(length), c = std::cos(-.38), sn = std::sin(-.38), bounds = s.radius + 2;
        p.shader(6 - bounds, -26 - bounds, 6 + bounds, -26 + bounds, [&](double x, double y, ShadeOutput& out) {
            const double dx = x - 6, dy = y - (-26), lx = dx * c + dy * sn, ly = (-dx * sn + dy * c) / .6,
                         r = jsHypot2(lx, ly);
            if (r > s.radius + .5 || r < s.radius - width - 1) return;
            double u = std::fmod(direction * (std::atan2(ly, lx) - angle), TAU);
            if (u < 0) u += TAU;
            if (u > span) return;
            const double v = u / span, w = width * std::pow(std::sin(PI * std::pow(v, 2.2)), .6);
            if (w < .35) return;
            const double q = (s.radius - r) / w;
            if (q < 0 || q > 1) return;
            const int ci = q < .22 && age < .5 ? 0 : int(jsRound(q * (n - 1) * .75 + age * 2.4 - v * .8 + .4));
            out.c        = ramp[size_t(n - 1 - std::clamp(ci, 0, n - 1))];
            out.a        = std::min(1., v / .4);
        });
        for (double u = 0; u <= span; u += .3) {
            const auto q = onArc(s, s.radius - width * .4, angle + direction * u);
            p.glow(q.x, q.y, 5 + width, ramp[size_t(n - 3)], .22 * (1 - age) * (u / span));
        }
    }
    void drawSlash(Paint& p, const SwordArc& s, std::span<const uint32_t> ramp) const {
        const double u = t - s.t0;
        if (u < 0) return;
        const double span = s.a1 - s.a0, head = ease::outExpo(clamp(u / s.sweep)),
                     tail = smooth(s.sweep + s.hold, s.sweep + s.hold + s.trail, u),
                     age  = clamp(u / (s.sweep + s.hold + s.trail));
        if (tail < 1) crescent(p, s, s.a0 + span * tail, span * (head - tail), s.width * (1 - tail * .55), age, ramp);
        const double gain = .7 * (1 - clamp((u - s.sweep - s.hold) / (s.trail + .2)));
        if (tail > .02 && gain > 0)
            for (double k = 0; k <= tail; k += .6 / (std::abs(span) * s.radius)) {
                const auto q = onArc(s, s.radius - .5, s.a0 + span * k);
                p.dot(q.x, q.y, ramp[1], gain * (.4 + .6 * k));
            }
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        drawSlash(p, echo, std::span<const uint32_t>(C).first(6));
        drawSlash(p, main, C);
        const double g = t - main.sweep * .6;
        if (g > 0 && g < .22)
            p.sparkle(tip.x, tip.y, 9 * ease::outBack(std::min(1., g / .05)) * (1 - g / .22), C[5], C[3], .6);
        if (t < .3) {
            const auto q =
                onArc(main, main.radius - 4, main.a0 + (main.a1 - main.a0) * ease::outExpo(clamp(t / main.sweep)));
            p.glow(q.x, q.y, 40, C[3], .5 * (1 - t / .3));
        }
        ps.draw(p);
    }
};
}  // namespace
const Descriptor& swordSlashDescriptor() {
    static const std::array<Option, 3> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks},
                                                {"radius", 0, 512, false, 0, &PixelFxMeleeSettings::radius}}};
    static const Descriptor            d{
        {PixelFxKind::SwordSlash, "melee/sword-slash", 136, 104, 44, 92, .75, false},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 2;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8), hex(0x62c8f4),
                              hex(0xb8c9e8), hex(0xe8f1ff), WHITE};
            s.melee.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
            s.melee.radius = 42;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SwordSlash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
