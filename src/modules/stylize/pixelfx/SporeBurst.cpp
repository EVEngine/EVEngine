// eve-hot-path
// Native adaptation of pixelfx nature/spore-burst.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/NatureGas.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                               GROW = .32, PUFF = .8, SINK = 2.15, D = 3.2;
constexpr std::array<std::array<double, 3>, 6> SPOTS{
    {{{-.45, .45, 1.4}}, {{.1, .72, 1.8}}, {{.55, .38, 1.2}}, {{-.12, .25, 1}}, {{.3, .12, .9}}, {{-.7, .16, .8}}}};
void toadstool(Paint& p, std::span<const uint32_t> K, std::span<const uint32_t> Sm, std::span<const uint32_t> Cs,
               double g, double sx, double sy, double glow, double sink) {
    const double H = 17 * g, cw = 17 * sx * g, ch = 13 * sy * g, base = sink, top = base - H;
    if (g < .05) return;
    for (double y = std::floor(top); y <= std::min(0., base); ++y)
        for (int x = -4; x <= 4; ++x) {
            const double w = 3 + ((y - top) / H) * 1;
            if (std::abs(x + .5) > w) continue;
            const double q = (x + .5 + w) / (2 * w);
            p.px(x, y, std::abs(x + .5) > w - .8 ? Sm[1] : q < .4 ? Sm[4] : q < .7 ? Sm[3] : Sm[2]);
        }
    const double ring = top + H * .3;
    if (ring <= 0)
        for (int x = -5; x <= 5; ++x) p.px(x, ring + (std::abs(x) > 3 ? 1 : 0), std::abs(x) > 4 ? Sm[1] : Sm[3]);
    const double gy = top + 1;
    if (gy <= 0)
        for (int x = int(std::floor(-cw)); x <= int(std::ceil(cw)); ++x) {
            const double d = std::abs(x + .5) / cw;
            if (d > .95) continue;
            p.px(x, gy, d < .3 ? Cs[0] : (x & 1) ? Sm[2] : Sm[3]);
            if (glow > .05) p.px(x, gy, glow > .6 && (x & 1) ? K[7] : K[5]);
        }
    if (glow > .05) p.glow(0, gy + 2, cw * 1.5 + 6, K[5], .5 * glow);
    p.shader(-cw - 1, top - ch - 1, cw + 1, top + 1, [&](double x, double y, ShadeOutput& out) {
        const double dx = x / cw, dy = (top - y) / ch, d = dx * dx + dy * dy;
        if (dy < -.12 || d > 1.12 || y > 0) return;
        if (d > 1) {
            out.c = Cs[0];
            return;
        }
        int i = dx < -.35 && dy > .45 ? 5 : dx < .05 && dy > .3 ? 4 : dy < .18 ? 2 : dx > .55 ? 2 : 3;
        for (const auto& spot : SPOTS) {
            const double ex = (dx - spot[0]) * cw, ey = (dy - spot[1]) * ch, r = spot[2];
            if (ex * ex + ey * ey * 1.6 < r * r * g * g) i = ex < 0 && ey > 0 ? 9 : 8;
        }
        out.c = i == 9 ? Sm[4] : i == 8 ? Sm[3] : Cs[size_t(i)];
    });
    p.px(-cw * .45, top - ch * .8, Cs[5]);
}
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct SporeBurst final : Kernel {
    const std::vector<uint32_t> K, Cs, Sm;
    Rng                         R;
    Particles                   ps;
    nature::Gas                 gas;
    Lights                      lights;
    double                      t      = 0;
    bool                        puffed = false, grown = false;
    explicit SporeBurst(const PixelFxSettings& o) : K(o.palette), Cs(o.nature.cap), Sm(o.nature.stem), R(o.seed) {
        ps.sparks = {K[7], K[6], K[5], K[4]};
        ps.haze   = K[5];
    }
    void   emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    double grow() const { return ease::outBack(clamp(t / GROW), 1.6) * (1 - ease::inCubic(clamp((t - SINK) / .45))); }
    double charge() const { return smooth(GROW, PUFF, t); }
    std::array<double, 2> shape() const {
        if (t < PUFF) {
            const double k = charge(), j = std::sin(t * 70) * .04 * k;
            return {1 + .28 * k + j, 1 + .22 * k - j};
        }
        const double e = t - PUFF, sp = std::exp(-e * 7) * std::cos(e * 26);
        return {1 + .25 * sp, 1 - .35 * sp};
    }
    void update(double dt) override {
        t += dt;
        const double f = std::min(1., dt * 120);
        if (!grown) {
            grown = true;
            for (int i = 0; i < 10; ++i)
                emit({ParticleKind::Grit, R.range(-4, 4), -1, R.range(-50, 50), -R.range(40, 120), R.range(.5, .8),
                      R.pick<uint32_t>(std::array{Sm[1], Sm[2]}), 1});
            for (int i = 0; i < 4; ++i)
                emit({ParticleKind::Dust, R.range(-4, 4), -1, R.signedRange(10, 40), -R.range(4, 12), R.range(.4, .7),
                      Sm[1], R.range(2, 3)});
        }
        if (t > GROW && t < PUFF - .05 && R.isChance(.25 * f)) {
            const double a = R.range(PI * 1.05, PI * 1.95), d = R.range(20, 38), x = std::cos(a) * d,
                         y = -18 + std::sin(a) * d * .7;
            emit({ParticleKind::Streak, x, y, -x * 3, (-18 - y) * 3, R.range(.2, .3), K[6]});
        }
        if (!puffed && t >= PUFF) {
            puffed = true;
            lights.add(0, -22, 30, WHITE, .6, .1);
            lights.add(0, -28, 70, K[5], .55, .9);
            for (int i = 0; i < 30; ++i) {
                const double a = -PI / 2 + R.range(-1.1, 1.1), sp = R.range(15, 95);
                // Aggregate evaluation preserves source argument/RNG order.
                const std::array<double, 9> q{t,
                                              R.range(-8, 8),
                                              -20 + R.range(-3, 3),
                                              std::cos(a) * sp * 1.3,
                                              std::sin(a) * sp - 10,
                                              R.range(3, 5),
                                              R.range(8, 14),
                                              t + R.range(1.6, 2.2),
                                              R.range(5, 10)};
                gas.add(q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8]);
            }
            for (int i = 0; i < 6; ++i) {
                const double                sd = R.sign();
                const std::array<double, 9> q{t,
                                              sd * R.range(6, 12),
                                              -5,
                                              sd * R.range(30, 70),
                                              -R.range(2, 14),
                                              R.range(2, 3),
                                              R.range(4, 6),
                                              t + R.range(.8, 1.1),
                                              R.range(4, 8)};
                gas.add(q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8]);
            }
            for (int i = 0; i < 40; ++i) {
                const double a = -PI / 2 + R.range(-1.4, 1.4), sp = R.range(40, 160);
                const bool   mote = R.isChance(.5);
                const double k    = mote ? .35 : .7;
                emit({mote ? ParticleKind::Mote : ParticleKind::Flake, R.range(-5, 5), -20, std::cos(a) * sp * k,
                      std::sin(a) * sp * k, R.range(1, 2), R.pick<uint32_t>(std::array{K[7], K[6], K[6], K[5]}),
                      R.range(.6, 1.6)});
            }
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Star, R.range(-20, 20), -R.range(12, 40), 0, -6, R.range(.3, .6), K[7],
                      R.range(1, 2.4), R.range(.05, .4)});
            for (int i = 0; i < 8; ++i)
                emit({ParticleKind::Dust, R.signedRange(4, 10), -1, R.signedRange(30, 90), -R.range(2, 10),
                      R.range(.5, .9), K[3], R.range(2, 3.5)});
        }
        if (puffed && t < 2.6 && R.isChance(.12 * f))
            emit({ParticleKind::Flake, R.range(-40, 40), -R.range(30, 70), R.range(-8, 8), R.range(4, 14),
                  std::min(R.range(.9, 1.5), D - .05 - t), R.isChance(.5) ? K[7] : K[6], R.range(.6, 1.5)});
        if (t > SINK && t < SINK + .4 && R.isChance(.15 * f))
            emit({ParticleKind::Dust, R.range(-5, 5), -1, R.signedRange(10, 30), -R.range(2, 8), R.range(.3, .5), Sm[1],
                  R.range(1.5, 2.5)});
        gas.prune(t);
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k = charge() * (1 - smooth(PUFF + .2, PUFF + 1.2, t));
        if (k > 0)
            for (int x = -20; x <= 20; ++x) p.dot(x, 0, std::abs(x) < 10 ? K[5] : K[4], k * (1 - std::abs(x) / 21.));
        p.glow(0, -2, 30, K[4], .25 * k);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const auto   sh = shape();
        const double gl =
            t < PUFF ? charge() * (.75 + .25 * std::sin(t * (20 + 30 * charge()))) : std::max(0., 1 - (t - PUFF) * 1.5);
        toadstool(p, K, Sm, Cs, grow(), sh[0], sh[1], gl, ease::inCubic(clamp((t - SINK) / .45)) * 20);
        gas.draw(p, t, K[2], K[4], K[6], K[7], .62, K[6]);
        if (!gas.list.empty()) {
            double cx = 0, cy = 0, n = 0;
            for (const auto& q : gas.list) {
                const auto g = gas.at(q, t);
                cx += g.x * g.a;
                cy += g.y * g.a;
                n += g.a;
            }
            if (n > .2) p.glow(cx / n, cy / n, 46, K[5], std::min(.32, .03 * n));
        }
        const double e = t - PUFF;
        if (e > 0 && e < .1) p.sparkle(0, -20, 10 * (1 - e / .1), K[7], K[6], .6);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& sporeBurstDescriptor() {
    static const std::array<Option, 2> options{{{"cap", 1, 64, false, 6, &PixelFxNatureSettings::cap},
                                                {"stem", 1, 64, false, 5, &PixelFxNatureSettings::stem}}};
    static const Descriptor            d{
        {PixelFxKind::SporeBurst, "nature/spore-burst", 212, 132, 106, 118, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 9;
            s.palette     = {hex(0x06141c), hex(0x0c2a34), hex(0x11484e), hex(0x17706a),
                             hex(0x22a08a), hex(0x4cd0a8), hex(0xa8f4d0), hex(0xf0fff6)};
            s.nature.cap  = {hex(0x1a0c1e), hex(0x3a1438), hex(0x62204e), hex(0x8e3462), hex(0xbc5276), hex(0xe88a96)};
            s.nature.stem = {hex(0x2a2230), hex(0x5a4e5e), hex(0x9a8e98), hex(0xd8ccd0), hex(0xf6eeee)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<SporeBurst>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
