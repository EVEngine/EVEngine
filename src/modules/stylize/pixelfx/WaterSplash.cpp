// eve-hot-path
// Native adaptation of pixelfx water/splash.js (MIT), including Sprite Fusion blood droplet recipes.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/WaterSimulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double SY = .22, HIT = .16, JET = .3, TEAR = .11;
double height(double u) { return 19 * ease::outCubic(clamp(u / .09)) * (1 - ease::inQuad(clamp((u - .12) / .3))); }
double foot(double u) { return 3 + 7.5 * ease::outCubic(clamp(u / .45)); }
double lip(double u) { return foot(u) + height(u) * .42 + 3 * ease::outCubic(clamp(u / .25)); }
double jetH(double u) { return 26 * (ease::outCubic(clamp(u / .12)) - ease::inQuad(clamp((u - .15) / .28))); }
struct WaterSplash final : Kernel {
    std::vector<uint32_t> C;
    uint32_t              seed;
    bool                  stone;
    Rng                   rng;
    double                phi, t = 0;
    Particles             ps;
    water::Mist           mist;
    Lights                lights;
    water::Ripples        rings;
    water::Drops          drops;
    struct Plink {
        double x, y, s, age = 0;
    };
    struct Bubble {
        double x, z, t0, r;
        bool   popped = false;
    };
    struct Spike {
        double a, length;
    };
    std::vector<Plink>    plinks;
    std::vector<Bubble>   bubbles;
    std::array<Spike, 12> spikes;
    uint16_t              fired = 0;
    enum class Cue { Hit, Spray, Tear, Fall, Bead, Slump, Glug0, Glug1, Glug2 };
    enum class CueResult { NoChange, Triggered };
    CueResult cue(Cue id, double at) {
        const auto bit = uint16_t(1u << unsigned(id));
        if ((fired & bit) || t < at) return CueResult::NoChange;
        fired |= bit;
        return CueResult::Triggered;
    }
    explicit WaterSplash(const PixelFxSettings& o)
        : C(o.palette), seed(o.seed), stone(o.water.stone), rng(o.seed), phi(rng.range(0, TAU)) {
        for (size_t i = 0; i < spikes.size(); ++i)
            spikes[i] = {std::fmod((double(i) * TAU) / 12 + phi, TAU), rng.range(.2, .36)};
    }
    double scallop(double a, double h) const {
        return h * (.86 + .14 * noise(std::cos(a) * 2.2 + std::sin(a) * 1.3 + phi * 3, seed));
    }
    void onLand(const water::Drop& d) {
        rings.add(d.x, d.z, .6, 2 + d.s * 1.8, .34 + d.s * .1, {1, 3, .95, .06});
        plinks.push_back({d.x, d.z * SY, d.s});
        if (d.s > 1 && !d.small)
            for (int i = 0; i < d.s; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(10, 34);
                drops.add({d.x, d.z, .5, std::cos(a) * v, std::sin(a) * v, rng.range(55, 110), 1, 1, 0, true});
            }
    }
    void update(double dt) override {
        t += dt;
        if (cue(Cue::Hit, HIT) == CueResult::Triggered) {
            lights.add(0, -6, 52, C[5], .6, .45);
            lights.add(0, -4, 16, WHITE, .5, .08);
            rings.add(0, 0, 8, 56, 2, {2, 3.4, 1.35, .16, 1});
            rings.add(0, 0, 5, 38, 1.7, {1, 3, .9, .1, 0, .14});
            for (int i = 0; i < 20; ++i) {
                const double a = rng.range(0, TAU), v = rng.range(50, 125);
                drops.add({std::cos(a) * 3, std::sin(a) * 3, rng.range(1, 6), std::cos(a) * v, std::sin(a) * v,
                           rng.range(160, 270), rng.isChance(.25) ? 2. : 1., 4, rng.range(0, .04)});
            }
            for (int i = 0; i < 9; ++i) {
                const double s = i % 2 ? 1 : -1, x = s * rng.range(4, 12);
                mist.add({x, -rng.range(1, 7), s * rng.range(25, 70), -rng.range(4, 16), rng.range(2.5, 4),
                          rng.range(7, 11), rng.range(.9, 1.5), rng.isChance(.5) ? C[6] : C[5], rng.range(.3, .42),
                          rng.range(0, .1)});
            }
        }
        if (cue(Cue::Spray, HIT + .04) == CueResult::Triggered) {
            for (int i = 0; i < 20; ++i) {
                const double a = rng.range(0, TAU), x = std::cos(a) * 9, y = std::sin(a) * 9 * SY - rng.range(6, 15),
                             vx = std::cos(a) * rng.range(25, 90), vy = -rng.range(30, 100), life = rng.range(.7, 1.3);
                const auto   c    = rng.isChance(.5) ? WHITE : C[6];
                const double size = rng.range(.6, 1.5);
                ps.spawn(rng, ParticleKind::Flake, x, y, vx, vy, life, c, size);
            }
            for (int i = 0; i < 5; ++i) {
                const double x = rng.range(-13, 13), y = -rng.range(6, 20), life = rng.range(.25, .55),
                             size = rng.range(1, 2.4), delay = rng.range(0, .15);
                ps.spawn(rng, ParticleKind::Star, x, y, 0, -4, life, C[6], size, delay);
            }
        }
        if (cue(Cue::Tear, HIT + TEAR) == CueResult::Triggered) {
            const double h = height(TEAR), rt = lip(TEAR);
            for (const auto& f : spikes) {
                const double a = f.a + rng.range(-.08, .08), v = rng.range(32, 88), L = h * f.length;
                drops.add({std::cos(f.a) * (rt + L * .45), std::sin(f.a) * rt, h + L + 1, std::cos(a) * v,
                           std::sin(a) * v, rng.range(90, 190), rng.isChance(.3) ? 3. : 2.});
            }
        }
        const double u = t - HIT;
        if (u > .13 && u < .3 && rng.isChance(.3)) {
            const double a = rng.range(0, TAU), rt = lip(u), v = rng.range(15, 45);
            drops.add({std::cos(a) * rt, std::sin(a) * rt, scallop(a, height(u)) * rng.range(.6, 1), std::cos(a) * v,
                       std::sin(a) * v, rng.range(-10, 45), rng.isChance(.3) ? 2. : 1.});
        }
        if (cue(Cue::Fall, HIT + .3) == CueResult::Triggered)
            for (int i = 0; i < 8; ++i) {
                const double a = rng.range(0, TAU), r = lip(.3), v = rng.range(15, 40);
                drops.add({std::cos(a) * r, std::sin(a) * r, rng.range(3, 7), std::cos(a) * v, std::sin(a) * v,
                           rng.range(30, 80), rng.isChance(.4) ? 2. : 1.});
            }
        if (cue(Cue::Bead, HIT + JET + .12) == CueResult::Triggered) {
            drops.add({0, .2, jetH(.12) + 1, rng.range(-6, 6), 0, 55, 3});
            for (int i = 0; i < 3; ++i)
                mist.add({rng.range(-3, 3), -jetH(.12) + rng.range(2, 8), rng.range(-14, 14), -rng.range(4, 12), 2,
                          rng.range(4, 6), rng.range(.6, 1), C[6], .3});
        }
        if (cue(Cue::Slump, HIT + JET + .38) == CueResult::Triggered) {
            rings.add(0, 0, 3, 30, 1.5, {2, 2.6, .9});
            for (int i = 0; i < 4; ++i)
                mist.add({rng.range(-6, 6), -rng.range(1, 4), rng.range(-30, 30), -rng.range(3, 10), 2.5,
                          rng.range(6, 9), rng.range(.8, 1.2), C[5], .28});
        }
        constexpr std::array<double, 3> at{{.95, 1.2, 1.5}};
        for (size_t i = 0; i < at.size(); ++i)
            if (cue(Cue(unsigned(Cue::Glug0) + unsigned(i)), HIT + at[i]) == CueResult::Triggered)
                bubbles.push_back({rng.range(-5, 5), rng.range(-6, 6), t, rng.range(1.3, 2.3)});
        for (auto& b : bubbles)
            if (!b.popped && t - b.t0 > .2) {
                b.popped = true;
                rings.add(b.x, b.z, 1, 7, .6, {1, 3, .9});
                plinks.push_back({b.x, b.z * SY, 1});
                for (int i = 0; i < 2; ++i)
                    drops.add({b.x, b.z, 1, rng.range(-20, 20), rng.range(-10, 10), rng.range(50, 90), 1, 1, 0, true});
            }
        for (size_t remaining = plinks.size(); remaining > 0; --remaining) {
            const size_t i = remaining - 1;
            if ((plinks[i].age += dt) > .14) plinks.erase(plinks.begin() + std::ptrdiff_t(i));
        }
        drops.update(dt, [&](const water::Drop& d) { onLand(d); });
        rings.update(dt);
        mist.update(dt);
        ps.update(dt, rng);
        lights.update(dt);
    }
    void crown(Paint& p, bool back) const {
        const double u = t - HIT, h = height(u);
        if (u < 0 || h < .7) return;
        const double r0 = foot(u), rt = lip(u);
        const bool   white = u < .045;
        // rt<22 and h<=19 bound the sheet to [-32,32)^2. Numeric slots and ordered
        // cells preserve the source Map's insertion order and strictly-higher-rank replacements.
        struct Cell {
            int      x, y;
            uint32_t color;
            double   alpha;
            int      rank;
        };
        std::array<int16_t, 4096> slots;
        slots.fill(-1);
        std::array<Cell, 4096> cells;
        size_t                 count = 0;
        auto                   put   = [&](double x, double y, uint32_t c, double a, int rank) {
            const int                   X = int(std::floor(x)), Y = int(std::floor(y));
            [[maybe_unused]] const bool inBounds = X >= -32 && X < 32 && Y >= -32 && Y < 32;
            EV_ASSERT(inBounds, "Source splash sheet bounds");
            auto& slot = slots[size_t(Y + 32) * 64 + size_t(X + 32)];
            if (slot < 0) {
                slot           = int16_t(count);
                cells[count++] = {X, Y, c, a, rank};
            } else if (rank > cells[size_t(slot)].rank)
                cells[size_t(slot)] = {X, Y, c, a, rank};
        };
        const int n = int(std::ceil(rt * TAU * 1.6));
        for (int i = 0; i < n; ++i) {
            const double a = (double(i) / n) * TAU, s = std::sin(a), c = std::cos(a);
            if ((s < 0) != back) continue;
            const uint32_t lane   = jsUint(std::floor(((a + phi) * rt) / 2.2));
            const bool     streak = rand({lane, 31}) < .3;
            const double   H = scallop(a, h), keep = 1 - clamp((u - .13 - .14 * rand({lane, 7})) / .1);
            const int      steps = int(std::ceil(H * 1.4)) + 2;
            for (int k = 0; k <= steps * keep; ++k) {
                const double v = double(k) / steps, r = r0 + (rt - r0) * std::pow(v, 2.2), x = c * r,
                             y = s * r * SY - H * v;
                if (white)
                    put(x, y, WHITE, 1, 3);
                else if (back)
                    put(x, y, v > .72 ? (streak ? C[5] : C[4]) : streak ? C[3] : C[2], 1, 1);
                else if (std::abs(c) > .9)
                    put(x, y, c < 0 ? (std::abs(c) > .97 ? WHITE : C[6]) : C[4], 1, 2);
                else if (v < .1)
                    put(x, y, v < .05 ? WHITE : C[6], 1, 2);
                else if (streak)
                    put(x, y, c < -.2 ? C[6] : C[5], 1, 1);
                else
                    put(x, y, c < 0 ? C[5] : C[4], .22, 0);
            }
            if (!white && keep >= 1) {
                const double x = c * rt, y = s * rt * SY - H;
                put(x, y, back ? C[5] : c < .45 ? WHITE : C[6], 1, 3);
                if (!back && c < .2) put(x, y + 1, C[6], 1, 2);
            }
        }
        for (size_t i = 0; i < count; ++i) {
            const auto& q = cells[i];
            if (q.rank == 0)
                p.blend(q.x, q.y, q.color, q.alpha);
            else
                p.px(q.x, q.y, q.color);
        }
        const double reach = 1 - smooth(TEAR, TEAR + .1, u);
        for (const auto& f : spikes) {
            if (((std::sin(f.a) < 0) != back) || reach <= 0) continue;
            const double H = scallop(f.a, h), L = h * f.length * reach, x0 = std::cos(f.a) * rt,
                         y0 = std::sin(f.a) * rt * SY - H, x1 = x0 + std::cos(f.a) * L * .45, y1 = y0 - L;
            if (L >= 1) p.line(x0, y0, x1, y1, white ? WHITE : back ? C[4] : C[6]);
            if (u > .04 && u < TEAR) water::bead(p, x1 - (back ? 0 : .5), y1 - 1, 0, -1, back ? 1 : 2, C);
            if (!back && std::sin(f.a) > .3) p.glow(x0, y0, 6, C[5], .22 * reach);
        }
    }
    void jet(Paint& p) const {
        const double u = t - HIT - JET, h = jetH(u);
        if (u < 0 || h < .8) return;
        const double top = -h;
        p.ellipse(0, 0, 5, 2.4, [&](double d, double a) {
            return std::sin(a) > 0 ? 0u : d < .5 ? WHITE : std::cos(a) < 0 ? C[6] : C[5];
        });
        p.shader(-3, top - 2, 3, 0, [&](double x, double y, ShadeOutput& out) {
            const double v = clamp((y - top) / h), w = 1.15 + 1.5 * v * v * v, q = x / w;
            if (std::abs(q) <= 1) out.c = q < -.5 ? C[6] : q < .3 ? WHITE : q < .72 ? C[5] : C[4];
        });
        if (u < .12) {
            p.circle(0, top - 1, 1.7, C[5]);
            p.circle(-.3, top - 1.3, 1.1, C[6]);
            p.px(-1, top - 2, WHITE);
        }
        p.glowLine(0, -1, 0, top, 7, C[5], .3);
    }
    void drawBack(Paint& p) const override {
        const double u = t - HIT;
        if (u > 0 && u < .26) {
            const double r = foot(u) * .85 * (1 - smooth(.12, .26, u));
            p.ellipse(0, 0, r, r * SY + .6, [&](double, double a) { return std::sin(a) < 0 ? C[1] : C[2]; });
        }
        rings.draw(p, C, -1);
        crown(p, true);
        drops.draw(p, C, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double u = t - HIT;
        rings.draw(p, C, 1);
        if (u > .2) {
            const double rx = 4 + 11 * ease::outCubic(clamp((u - .2) / .7)),
                         k  = smooth(.2, .32, u) * (1 - smooth(.5, 2, u)) * .9;
            water::lace(p, 0, 0, rx, rx * SY + .5, k, C, t, seed);
        }
        crown(p, false);
        jet(p);
        for (const auto& b : bubbles) {
            const double g = (t - b.t0) / .2;
            if (g > 1) continue;
            const double r = b.r * ease::outBack(g), x = b.x, y = b.z * SY;
            for (double a = PI; a <= TAU + .01; a += .5 / std::max(1., r))
                p.dot(x + std::cos(a) * r, y + std::sin(a) * r, C[6]);
            p.px(x - r * .4, y - r * .6, WHITE);
            p.glow(x, y - 1, 4, C[5], .25);
        }
        for (const auto& q : plinks) {
            const double k = q.age / .14;
            if (k < .5) p.px(q.x, q.y, WHITE);
            if (q.s > 1) p.line(q.x, q.y - 1, q.x, q.y - 1 - 3 * (1 - k), k < .4 ? WHITE : C[6]);
            p.glow(q.x, q.y, 4, C[5], .3 * (1 - k));
        }
        drops.draw(p, C, Paint::Layer::Front);
        if (u >= 0 && u < .07) {
            p.ellipse(0, 0, 10, 2.4, [&](double d) { return d < .6 ? WHITE : C[6]; });
            p.sparkle(0, -3, 5 * (1 - u / .07), C[6], C[5], .5);
        }
        if (stone && t < HIT) {
            const double v = t / HIT, y = -70 + 70 * (.4 * v + .6 * v * v);
            p.lineFn(0, y - 12, 0, y - 3, water::rock()[4], [](double w) { return w * .8; });
            water::pebble(p, 0, y, 2.6, water::rock());
        }
        mist.draw(p, C[5]);
        ps.draw(p);
        lights.draw(p);
        if (u > 0) {
            p.glow(0, -2, 34, C[4], .2 * (1 - smooth(0, 1.6, u)));
            p.glow(0, 2, 64, C[3], .16 * (1 - smooth(.1, 2.1, u)));
        }
    }
};
}  // namespace
const Descriptor& waterSplashDescriptor() {
    static const std::array<Option, 1> options{{{"stone", 0, 1, false, 0, &PixelFxWaterSettings::stone}}};
    static const Descriptor            d{
        {PixelFxKind::WaterSplash, "water/splash", 176, 100, 88, 78, 2.5, false},
        8,
        [](PixelFxSettings& s) {
            water::defaults(s, 3);
            s.water.stone = true;
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<WaterSplash>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
