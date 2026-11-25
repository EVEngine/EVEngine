// eve-hot-path
// Native adaptation of pixelfx ui/health-bar-hit.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiHud.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double      HIT = .2, DRAIN = .62, DRAINED = 1.05, FOLD = 1.62, BX = -48, BW = 104, BY = -4, BH = 8;
const ui::PanelColors INK{hex(0x0d0a14), hex(0x4a4060), hex(0x2a2338), hex(0x1e1929), hex(0x141019)};
struct HealthBarHit final : Kernel {
    const std::vector<uint32_t>     C;
    Rng                             R;
    const uint32_t                  seed, pale;
    const double                    from, to;
    const std::array<uint32_t, 256> heart;
    Particles                       ps;
    Bits                            bits;
    Lights                          lights;
    double                          t   = 0;
    bool                            hit = false;
    explicit HealthBarHit(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          seed(o.seed),
          pale(mix(C[6], hex(0xffe9a8), .5)),
          from(clamp(o.ui.from)),
          to(clamp(std::min(o.ui.to, from))),
          heart(ui::heartPalette(C)) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
    }
    double ghostAt(double at) const {
        return at < DRAIN ? from : from - (from - to) * ease::inOutQuad(seg(at, DRAIN, DRAINED));
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(BX + BW * (to + from) / 2, BY + 4, 50, C[5], .7, .3);
            lights.add(BX + BW * (to + from) / 2, BY + 4, 24, WHITE, .6, .08);
            const std::array<uint32_t, 4> colors{C[7], C[6], C[5], C[4]};
            for (int i = 0; i < 12; ++i) {
                const double x = BX + BW * R.range(to, from), a = -PI / 2 + R.range(-1.2, 1.2),
                             speed = R.range(50, 130);
                Debris       b;
                b.kind            = DebrisKind::Shard;
                b.x               = x;
                b.y               = BY + R.range(1, 7);
                b.vx              = std::cos(a) * speed;
                b.vy              = std::sin(a) * speed;
                b.gravity         = .45;
                b.drag            = 1.2;
                b.life            = R.range(.35, .6);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-20, 20);
                b.size            = R.range(1.5, 3);
                b.color           = R.pick<uint32_t>(colors);
                bits.add(std::move(b));
            }
            for (int i = 0; i < 10; ++i) {
                const double a = R.range(0, TAU), speed = R.range(60, 160), x = BX + BW * R.range(to, from),
                             life = R.range(.12, .26);
                ps.spawn(R, ParticleKind::Spark, x, BY + 4, std::cos(a) * speed, std::sin(a) * speed * .6 - 30, life);
            }
        }
        if (t > DRAIN && t < DRAINED && R.isChance(.5)) {
            const double x = BX + BW * ghostAt(t), y = BY + R.range(1, 7), vx = R.range(-20, 60), vy = -R.range(20, 70),
                         life = R.range(.1, .22);
            ps.spawn(R, ParticleKind::Spark, x, y, vx, vy, life);
        }
        bits.update(dt);
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto   jitter = ui::shake(t, 3, HIT, .28, seed);
        const double dip    = t > HIT ? jsRound(3 * std::exp(-(t - HIT) * 10) * std::cos((t - HIT) * 24)) : 0,
                     open   = ease::outBack(seg(t, 0, .14), 2) * (1 - ease::inBack(seg(t, FOLD, FOLD + .18), 2));
        if (open > .04) {
            const double ox = jitter.x, oy = jitter.y + dip, h = 18 * open, y0 = -9 + (18 - h) / 2;
            auto         ink = INK;
            if (t >= HIT && t < HIT + .07) {
                ink.outline = C[4];
                ink.rim     = C[6];
            }
            ui::panel(p, BX - 22 + ox, y0 + oy, BW + 26, h, ink, std::min(5., h / 2));
            if (open > .75) {
                const double val = t < HIT ? from : to, ghost = t < HIT ? from : ghostAt(t);
                const bool   white = t >= HIT && t < HIT + .07;
                p.rect(BX + ox, BY + oy, BW, BH, hex(0x0d0a14));
                p.shader(BX + ox, BY + oy, BX + BW + ox, BY + BH + oy, [&](double x, double y, ShadeOutput& out) {
                    const double u = (x - BX - ox) / BW, v = y - BY - oy;
                    out.c = u < val     ? (v < 1   ? C[6]
                                           : v < 2 ? C[5]
                                           : v < 5 ? C[4]
                                           : v < 7 ? C[3]
                                                   : C[2])
                            : u < ghost ? (white   ? WHITE
                                           : v < 1 ? WHITE
                                           : v < 6 ? pale
                                                   : C[5])
                            : v < 1     ? hex(0x2a2338)
                                        : hex(0x191424);
                });
                for (int i = 1; i < 10; ++i) p.rect(BX + ox + jsRound((BW * i) / 10), BY + oy, 1, BH, hex(0x0d0a14));
                if (t > HIT) p.rect(BX + ox + jsRound(BW * val) - 1, BY + oy, 1, BH, C[7]);
                if (t > DRAIN && t < DRAINED) {
                    const double gx = BX + ox + BW * ghost;
                    p.rect(gx - 1, BY + oy - 1, 2, BH + 2, WHITE);
                    p.glow(gx, BY + oy + 4, 10, C[5], .6);
                }
                p.glow(BX + ox + BW * val * .5, BY + oy + 4, BW * .4, C[3], .12);
                const double hk  = t - HIT,
                             hop = hk > 0 ? jsRound(-3 * std::exp(-hk * 12) * std::abs(std::cos(hk * 22))) : 0;
                p.sprite(ui::HEART, heart, BX - 17 + ox, oy - 6 + hop);
                if (hk > 0 && hk < .07) {
                    auto flash = heart;
                    flash['k'] = 0;
                    flash['w'] = WHITE;
                    flash['h'] = WHITE;
                    flash['o'] = WHITE;
                    flash['m'] = WHITE;
                    flash['d'] = C[6];
                    p.sprite(ui::HEART, flash, BX - 17 + ox, oy - 6 + hop);
                }
                p.glow(BX - 11 + ox, oy, 10, C[4], .25 + (hk > 0 && hk < .2 ? .4 : 0));
                if (t < HIT && std::fmod(t * 2.5, 1) < .3) p.sparkle(BX - 14 + ox, oy - 3, 2, C[7], C[6], .3);
            }
        }
        bits.draw(p);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& healthBarHitDescriptor() {
    static const std::array<Option, 2> options{
        {{"from", 0, 1, false, 0, &PixelFxUiSettings::from}, {"to", 0, 1, false, 0, &PixelFxUiSettings::to}}};
    static const Descriptor d{
        {PixelFxKind::HealthBarHit, "ui/health-bar-hit", 160, 64, 80, 30, 2, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 3;
            s.ui.from = .8;
            s.ui.to   = .45;
            s.palette = {hex(0x1a0508), hex(0x4a0b12), hex(0x8c1420), hex(0xd42a2e),
                         hex(0xff5a4a), hex(0xff9a86), hex(0xffd8cc), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HealthBarHit>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
