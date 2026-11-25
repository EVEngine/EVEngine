// eve-hot-path
// Native adaptation of pixelfx impacts/crit-hit.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Key {
    double                time, scale, thickness;
    std::array<size_t, 4> colors;
};
constexpr std::array<Key, 4> KEYS{{{0, 1, 1, {6, 7, 7, 7}},
                                   {.025, 1.3, 1, {2, 5, 6, 7}},
                                   {.058, 1.05, .72, {2, 4, 5, 6}},
                                   {.092, 1.22, .3, {1, 2, 3, 4}}}};
void                         flare(Paint& p, double y, double L, double k, std::span<const uint32_t> C) {
    for (const auto ray :
         std::array<std::array<double, 3>, 4>{{{1, 0, L}, {0, 1, L * .45}, {.7, .7, L * .16}, {.7, -.7, L * .16}}})
        for (int side : {-1, 1})
            for (int d = 1; d <= ray[2]; ++d) {
                const double u = d / ray[2], a = u < .7 ? 1 : k * (1.6 - u);
                p.dot(side * ray[0] * d, y + side * ray[1] * d, u < .35 ? WHITE : u < .7 ? C[6] : C[5], a);
                if (ray[1] == 0 && u < .42)
                    for (int e : {-1, 1}) p.dot(side * d, y + e, u < .2 ? C[6] : C[5], u < .3 ? 1 : k);
            }
    p.glowLine(-L, y, L, y, 6, C[6], .5 * k);
    p.glowLine(0, y - L * .45, 0, y + L * .45, 4, C[6], .3 * k);
}
struct CritHit final : Kernel {
    const std::vector<uint32_t>        C;
    const double                       Y, aim;
    Rng                                rng;
    Particles                          ps;
    Bits                               bits;
    Lights                             lights;
    std::vector<impact::Spike>         spikes;
    std::array<impact::RingSegment, 7> segments;
    double                             t = 0;
    explicit CritHit(const PixelFxSettings& o) : C(o.palette), Y(-o.impact.height), aim(o.impact.aim), rng(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        impact::SpikeRecipe recipe;
        recipe.minLength = 11;
        recipe.maxLength = 15;
        recipe.minWidth  = 1.8;
        recipe.maxWidth  = 2.5;
        recipe.forward   = 1.4;
        recipe.jag       = .5;
        spikes           = impact::spikes(rng, 14, aim, recipe);
        std::array<size_t, 14> order{};
        for (size_t i = 0; i < 14; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return spikes[a].length > spikes[b].length; });
        for (size_t i = 0; i < 2; ++i) spikes[order[i]].length *= 1.3;
        for (size_t i = 0; i < 7; ++i) {
            const double a = aim + (double(i) / 7) * TAU, gap = rng.range(.55, .85),
                         drift = rng.range(.08, .4) + .15 * std::max(0., std::cos(a - aim));
            segments[i]        = {a, gap, drift, .7 + .5 * std::max(0., std::cos(a - aim))};
        }
        lights.add(0, Y, 84, C[5], .6, .5);
        lights.add(0, Y, 30, WHITE, .7, .1);
        lights.add(0, -4, 44, C[4], .3, .45);
        for (int i = 0; i < 36; ++i) {
            const bool   back = rng.isChance(.22);
            const double a    = (back ? aim + PI : aim) + rng.range(-1, 1) * (back ? .6 : .8),
                         v = rng.range(170, 430) * (back ? .6 : 1), life = rng.range(.15, .5),
                         delay = rng.isChance(.3) ? rng.range(0, .04) : 0;
            ps.spawn(rng, ParticleKind::Spark, 0, Y, std::cos(a) * v, std::sin(a) * v - 50, life, WHITE, 1, delay);
        }
        const std::array<uint32_t, 4> shardColors{C[7], C[6], C[6], C[5]};
        for (int i = 0; i < 10; ++i) {
            const double a = aim + rng.range(-1, 1) * 1.1 - .35, v = rng.range(110, 280);
            Debris       b;
            b.kind            = DebrisKind::Shard;
            b.y               = Y;
            b.vx              = std::cos(a) * v;
            b.vy              = std::sin(a) * v - 40;
            b.gravity         = .8;
            b.drag            = .7;
            b.bounce          = .35;
            b.life            = rng.range(.55, .85);
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-25, 25);
            b.size            = rng.range(2, 4.5);
            b.color           = rng.pick<uint32_t>(shardColors);
            bits.add(std::move(b));
        }
        const std::array<uint32_t, 3> moteColors{C[6], C[5], C[5]};
        for (int i = 0; i < 12; ++i) {
            const double a = aim - PI / 2 + rng.range(-1.4, 1.4), v = rng.range(35, 110), life = rng.range(.35, .7);
            const auto   c     = rng.pick<uint32_t>(moteColors);
            const double delay = rng.range(0, .05);
            ps.spawn(rng, ParticleKind::Mote, std::cos(a) * 4, Y + std::sin(a) * 4, std::cos(a) * v,
                     std::sin(a) * v - 15, life, c, .6, delay);
        }
        for (int i = 0; i < 5; ++i) {
            const double a = rng.range(0, TAU), d = rng.range(14, 34), life = rng.range(.22, .38);
            const auto   c    = rng.isChance(.5) ? C[6] : WHITE;
            const double size = rng.range(1.6, 3), delay = rng.range(.08, .32);
            ps.spawn(rng, ParticleKind::Star, std::cos(a) * d, Y + std::sin(a) * d * .8, 0, -4, life, c, size, delay);
        }
    }
    void update(double dt) override {
        t += dt;
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        bits.update(dt, [](double) { return 0.; });
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double u = clamp((t - .03) / .37);
        if (t >= .03 && u < 1) {
            const double r = 10 + 30 * ease::outCubic(u), brk = smooth(.12, .6, u),
                         th = std::max(1., 3.6 * std::pow(1 - u, 1.2));
            const size_t i  = u < .12 ? 7 : u < .3 ? 6 : u < .5 ? 5 : u < .75 ? 4 : 3;
            impact::brokenRing(p, 0, Y, r, th, segments, brk, C[i], C[std::max(size_t(2), i - 2)],
                               u < .6 ? 1 : (1 - u) / .4, 1, 0);
            if (u < .6) p.glowRing(0, Y, r, 6, C[5], .35 * std::pow(1 - u / .6, 2));
        }
        bits.draw(p);
        ps.draw(p);
        const double f = (t - .04) / .28;
        if (f > 0 && f < 1) {
            flare(p, Y, 64 * std::pow(1 - f, 1.3), 1 - f, C);
            p.glow(0, Y, 56, C[5], .4 * (1 - f));
        }
        for (size_t i = 0; i < KEYS.size(); ++i)
            if (t >= KEYS[i].time && t < (i + 1 < KEYS.size() ? KEYS[i + 1].time : .125)) {
                const auto& k = KEYS[i];
                impact::star(p, 0, Y, spikes, k.scale, k.thickness,
                             {C[k.colors[0]], C[k.colors[1]], C[k.colors[2]], C[k.colors[3]]}, 1, 2.6);
            }
        const double pop = std::exp(-t / .05);
        if (pop > .02) {
            p.glow(0, Y, 44, C[5], .4 * pop);
            p.glow(0, Y, 12, WHITE, .6 * pop);
        }
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& critHitDescriptor() {
    static const std::array<Option, 2> options{{{"height", 0, 256, false, 0, &PixelFxImpactSettings::height},
                                                {"aim", -TAU, TAU, false, 0, &PixelFxImpactSettings::aim}}};
    static const Descriptor            d{
        {PixelFxKind::CritHit, "impacts/crit-hit", 216, 136, 84, 106, .9, false},
        8,
        [](PixelFxSettings& s) {
            s.seed          = 3;
            s.impact.height = 26;
            s.impact.aim    = .12;
            s.palette       = {hex(0x2a0510), hex(0x5c0a1c), hex(0xa3142a), hex(0xe0302c),
                               hex(0xff7a2a), hex(0xffc23d), hex(0xfff09a), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CritHit>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
