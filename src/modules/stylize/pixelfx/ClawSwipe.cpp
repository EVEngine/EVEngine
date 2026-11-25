// eve-hot-path
// Native adaptation of pixelfx melee/claw-swipe.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/MeleeSwing.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double RAKE = .05, STAGGER = .04, CLOSE0 = .16, CLOSE = .3;
struct Mark {
    melee::Swing  swing;
    double        t0, width;
    bool          open;
    melee::Travel end;
};
struct ClawSwipe final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         R;
    Particles                   ps;
    Bits                        drips;
    Lights                      lights;
    std::array<Mark, 3>         marks;
    double                      t = 0;
    explicit ClawSwipe(const PixelFxSettings& o) : C(o.palette), R(o.seed) {
        ps.sparks = o.melee.sparks;
        for (int i = 0; i < 3; ++i) {
            const auto s     = melee::through(31 + i * 8, -23 + i * 2, 1.84, 1, 40, .84 - i * .05, .5);
            marks[size_t(i)] = {s, i * STAGGER, 4.2 - i * .4, false, melee::travel(s, s.span)};
        }
    }
    void update(double dt) override {
        t += dt;
        for (auto& m : marks)
            if (!m.open && t >= m.t0 + RAKE * .7) {
                m.open = true;
                lights.add(m.end.x, m.end.y - 6, 34, C[4], .45, .3);
                for (int i = 0; i < 7; ++i) {
                    const double angle = std::atan2(m.end.dy, m.end.dx) + R.range(-.5, .5), speed = R.range(110, 260),
                                 life = R.range(.12, .32);
                    ps.spawn(R, ParticleKind::Spark, m.end.x, m.end.y, std::cos(angle) * speed, std::sin(angle) * speed,
                             life);
                }
                for (int i = 0; i < 5; ++i) {
                    const auto   q  = melee::onSwing(m.swing, m.swing.R, m.swing.span * R.range(.15, .9));
                    const double vx = R.range(-60, -10), vy = R.range(-30, 30), life = R.range(.2, .4);
                    const auto   color = R.isChance(.5) ? C[5] : C[4];
                    ps.spawn(R, ParticleKind::Mote, q.x, q.y, vx, vy, life, color, .5);
                }
                for (int i = 0; i < 2; ++i) {
                    const auto q = melee::onSwing(m.swing, m.swing.R - 1, m.swing.span * R.range(.35, .75));
                    Debris     b;
                    b.kind    = DebrisKind::Drip;
                    b.x       = q.x;
                    b.y       = q.y;
                    b.vx      = R.range(-8, 8);
                    b.vy      = R.range(10, 40);
                    b.gravity = .55;
                    b.life    = 1.2;
                    b.color   = C[3];
                    drips.add(std::move(b));
                }
            }
        drips.update(dt, [](double) { return 0.; });
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const int n = int(C.size());
        for (const auto& m : marks) {
            const double u = t - m.t0;
            if (u < 0) continue;
            const double span = m.swing.span, head = span * ease::outExpo(clamp(u / RAKE)),
                         closing = smooth(CLOSE0, CLOSE0 + CLOSE, u), lo = (span / 2) * closing,
                         hi = std::min(head, span - (span / 2) * closing), heat = clamp(1 - (t - m.t0) / .42);
            if (hi - lo > .02) {
                melee::smear(
                    p, m.swing, lo, hi, m.width,
                    [&](double uu, double r, ShadeOutput& out, double) {
                        const double v   = uu / span,
                                     w   = m.width * std::pow(std::sin(PI * v), 1.15) * (1 - .55 * closing),
                                     off = std::abs(r - (m.swing.R - m.width / 2)) / (w / 2 + .01);
                        if (w < .5 || off > 1) return;
                        const int ci =
                            int(jsRound((1 - heat) * 5.2 + off * 1.6 + (head < span ? (span - uu) / span : 0) * .5));
                        out.c = C[size_t(n - 1 - std::clamp(ci, 0, n - 2))];
                    },
                    0, 2);
                melee::glowAlong(p, m.swing, m.swing.R - m.width / 2, lo, hi, 11, C[4], .42 * heat * heat, 0, .2);
            }
            const double k = .8 * (1 - clamp((u - CLOSE0 - CLOSE * .5) / .4));
            if (closing > .05 && k > 0) {
                melee::trace(p, m.swing, m.swing.R - m.width / 2, .04 * span, lo, C[2], k);
                melee::trace(p, m.swing, m.swing.R - m.width / 2, span - lo, .96 * span, C[2], k);
            }
            const double g = u - RAKE * .4;
            if (g > 0 && g < .16)
                p.sparkle(m.end.x, m.end.y, 5 * ease::outBack(std::min(1., g / .04)) * (1 - g / .16), C[6], C[4], .4);
        }
        drips.draw(p);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& clawSwipeDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxMeleeSettings::sparks}}};
    static const Descriptor            d{
        {PixelFxKind::ClawSwipe, "melee/claw-swipe", 128, 96, 36, 84, .9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 8;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x2a0808), hex(0x5c0f12), hex(0xa51d1d), hex(0xe2401f),
                              hex(0xff8a2a), hex(0xffd166), hex(0xfff3c4), WHITE};
            s.melee.sparks = {hex(0xfff3c4), hex(0xffd166), hex(0xff8a2a), hex(0xc42a1a)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ClawSwipe>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
