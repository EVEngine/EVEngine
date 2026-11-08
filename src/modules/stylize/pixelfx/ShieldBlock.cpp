// eve-hot-path
// Native adaptation of pixelfx impacts/shield-block.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Impact.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CX = 3, RX = 28, RY = 44, GRID = 3.4, HIT_LON = 1.18, HIT_LAT = .5, GONE = .5;
Point onDome(double lon, double lat) { return {CX + RX * std::cos(lat) * std::sin(lon), -RY * std::sin(lat)}; }
struct ShieldBlock final : Kernel {
    const std::vector<uint32_t> C, S;
    const Point                 hit;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::vector<impact::Spike>  spikes;
    double                      t = 0;
    explicit ShieldBlock(const PixelFxSettings& o)
        : C(o.palette), S(o.impact.sparks), hit(onDome(HIT_LON, HIT_LAT)), rng(o.seed) {
        ps.sparks = {S[0], S[1], S[2], S[3]};
        impact::SpikeRecipe recipe;
        recipe.minLength = 5;
        recipe.maxLength = 8;
        recipe.minWidth  = 1.4;
        recipe.maxWidth  = 1.9;
        recipe.forward   = 1.2;
        recipe.back      = .3;
        spikes           = impact::spikes(rng, 8, .2, recipe);
        lights.add(hit.x, hit.y, 40, S[1], .7, .2);
        lights.add(hit.x, hit.y, 12, WHITE, .7, .06);
        const double normal = std::atan2(-std::sin(HIT_LAT) / RY, (std::cos(HIT_LAT) * std::sin(HIT_LON)) / RX);
        for (int i = 0; i < 14; ++i) {
            const double a = normal + rng.range(-1.2, .9), v = rng.range(130, 300), life = rng.range(.15, .42),
                         delay = i > 10 ? rng.range(.01, .04) : 0;
            ps.spawn(rng, ParticleKind::Spark, hit.x + 1, hit.y, std::cos(a) * v, std::sin(a) * v - 30, life, WHITE, 1,
                     delay);
        }
    }
    void dome(Paint& p, bool back) const {
        const double fade = 1 - smooth(GONE + .35, GONE + .55, t);
        if (fade <= 0) return;
        for (int y = -45; y < 0; ++y)
            for (int x = 3; x <= 32; ++x) {
                const double u = (x + .5 - CX) / RX, v = -(y + .5) / RY, q = u * u + v * v;
                if (q > 1 || u < 0 || v < 0) continue;
                const double z = std::sqrt(1 - q) * (back ? -1 : 1), lon = std::atan2(u, z), lat = std::asin(clamp(v)),
                             a = (lon + lat) * GRID, b = (lon - lat) * GRID, fa = a - std::floor(a),
                             fb   = b - std::floor(b);
                const double cell = rand({jsUint(std::floor(a)), jsUint(std::floor(b)), 31}), gone = GONE + cell * .35;
                const bool   line = std::min({fa, 1 - fa, fb, 1 - fb}) < .13;
                if (t > gone) continue;
                const double distance = std::acos(clamp(
                    std::cos(lat) * std::cos(HIT_LAT) * std::cos(lon - HIT_LON) + std::sin(lat) * std::sin(HIT_LAT), -1,
                    1));
                double       impact   = 0;
                for (const auto& wave : std::array<std::array<double, 2>, 2>{{{0, 1}, {.12, .55}}}) {
                    const double k = t - wave[0], front = k * 3.6;
                    if (k < 0 || k > .55) continue;
                    if (std::abs(distance - front) < .2)
                        impact = std::max(impact, wave[1] * (1 - k / .55));
                    else if (distance < front)
                        impact = std::max(impact, .3 * wave[1] * (1 - k / .55));
                }
                const double rim = 1 - std::abs(z);
                const bool flash = t<.05, blink = t> gone - .06,
                           sheen = std::abs(wrap(lat * .8 - lon * .3 - t * 1.6, 1) - .5) < .05;
                if (back) {
                    if (line) p.dot(x, y, C[3], .5 * fade);
                    continue;
                }
                if (q > .9)
                    p.dot(x, y, flash || impact > .5 ? WHITE : C[6], fade);
                else if (line)
                    p.dot(x, y,
                          flash || blink || impact > .5 ? WHITE
                          : impact > .2 || sheen        ? C[6]
                          : rim > .7                    ? C[5]
                          : rim > .35                   ? C[4]
                                                        : C[3],
                          fade);
                else
                    p.blend(x, y, flash || impact > .35 ? C[6] : C[3],
                            fade * std::min(.85, (flash ? .35 : .1 + .25 * rim * rim) + impact * .65));
                if (((x * 7 + y * 3) & 15) == 0) p.light(x, y, C[4], .15 + .5 * impact);
            }
    }
    void update(double dt) override {
        t += dt;
        if (t > GONE && t < GONE + .38 && rng.isChance(.5)) {
            const double lon = rng.range(0, PI / 2), lat = rng.range(.05, 1.4);
            const auto   point = onDome(lon, lat);
            const double vx = rng.range(-10, 10), vy = -rng.range(10, 40), life = rng.range(.2, .36);
            const auto   color = rng.isChance(.5) ? C[6] : C[5];
            const double size  = rng.range(.6, 1.4);
            ps.spawn(rng, ParticleKind::Star, point.x, point.y, vx, vy, life, color, size);
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override { dome(p, true); }
    void draw(Paint& p) const override {
        dome(p, false);
        const double fade = 1 - smooth(GONE + .3, GONE + .55, t);
        if (fade > 0)
            for (int x = 3; x <= 31; ++x) p.dot(x, 0, t < .05 ? WHITE : C[5], fade * (.6 + .4 * ((x - CX) / RX)));
        if (t < .03)
            impact::star(p, hit.x, hit.y, spikes, 1, 1, {S[3], S[0], S[1], WHITE}, 1, 2);
        else if (t < .06)
            impact::star(p, hit.x, hit.y, spikes, 1.25, .55, {S[3], S[1], S[2], S[0]}, 1, 2);
        ps.draw(p);
        p.glow(CX + RX * .5, -RY * .45, RY * .9, C[4],
               .2 * (1 - smooth(GONE, GONE + .55, t)) + .35 * std::exp(-t / .08));
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& shieldBlockDescriptor() {
    static const std::array<Option, 2> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor},
                                                {"sparks", 1, 64, false, 4, &PixelFxImpactSettings::sparks}}};
    static const Descriptor            d{
        {PixelFxKind::ShieldBlock, "impacts/shield-block", 148, 104, 40, 88, 1.1, false},
        8,
        [](PixelFxSettings& s) {
            s.seed          = 12;
            s.actor.source  = PixelFxActorSource::Dummy;
            s.palette       = {hex(0x151a3d), hex(0x212f6b), hex(0x2c58aa), hex(0x3a8bd8),
                               hex(0x62c8f4), hex(0xa6ecff), hex(0xdcfbff), WHITE};
            s.impact.sparks = {hex(0xfff3a6), hex(0xffe26b), hex(0xff8a2a), hex(0xf2561d)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ShieldBlock>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
