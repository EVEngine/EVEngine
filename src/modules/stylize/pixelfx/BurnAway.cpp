// eve-hot-path
// Native adaptation of pixelfx transitions/burn-away.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenField.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double CLOSE = 1.18, COVER = 1.35, SMOULDER = 1.5, POP = 1.8, OPEN = 2.62, SC = 90, HALO = 24, POOL = 48;
struct BurnGeometry {
    uint32_t             seed;
    screen::Field        A, B;
    double               ox = 0, oy = 0;
    std::vector<float>   heat, fleck, jit, vein;
    std::vector<uint8_t> ph;
    explicit BurnGeometry(uint32_t value) : seed(value * 13) {
        A = screen::field([&](double x, double y) {
            const double ux = (x - screen::W / 2.) / (screen::W / 2.), uy = (y - screen::H / 2.) / (screen::H / 2.),
                         m = std::pow(std::pow(ux, 4) + std::pow(uy, 4), .25);
            return (1 - m) * SC + 14 * fbm(x / 44, y / 44, seed, 3) + 2.4 * fbm(x / 7, y / 7, seed + 9, 2) +
                   7 * (.5 - y / screen::H);
        });
        Rng R((seed + 1) * 31 + 5);
        ox = screen::W / 2. + R.range(-26, 26);
        oy = screen::H / 2. + R.range(-14, 6);
        struct Sat {
            double x, y, d;
        };
        std::array<Sat, 2> sats{};
        for (size_t i = 0; i < 2; ++i) {
            const int    s = i == 0 ? -1 : 1;
            const double x = ox + s * R.range(70, 110), y = oy + R.range(-40, 40), d = R.range(30, 44);
            sats[i] = {x, y, d};
        }
        const uint32_t bs = seed + 1;
        B                 = screen::field([&](double x, double y) {
            double v = jsHypot2((x - ox) / (screen::W / 2.), (y - oy) / (screen::H / 2.)) * SC;
            for (const auto& s : sats)
                v = std::min(v, s.d + jsHypot2((x - s.x) / (screen::W / 2.), (y - s.y) / (screen::H / 2.)) * SC * 1.15);
            return v + 13 * fbm(x / 40, y / 40, bs + 21, 3) + 2.4 * fbm(x / 7, y / 7, bs + 33, 2);
        });
        heat.resize(screen::W * screen::H);
        ph.resize(heat.size());
        fleck.resize(heat.size());
        jit.resize(heat.size());
        vein.resize(heat.size());
        for (uint32_t i = 0; i < heat.size(); ++i) {
            const auto x = i % screen::W, y = i / screen::W;
            heat[i]         = float(.5 + .5 * noise2(x / 13., y / 13., seed + 41));
            ph[i]           = uint8_t(std::min(63., std::floor((.5 + .5 * noise2(x / 23., y / 23., seed + 43)) * 64)));
            fleck[i]        = float(rand({x, y, seed + 45}) < .04 ? 9 + 22 * rand({x, y, seed + 47}) : 0);
            jit[i]          = float((rand({x, y, seed + 49}) - .5) * 1.5);
            const double rx = x * .8 + y * .6, ry = y * .8 - x * .6;
            vein[i] = float(std::min(std::abs(noise2(rx / 11, ry / 9, seed + 61)),
                                     std::abs(noise2(ry / 5.5, rx / 5, seed + 63)) * 1.5));
        }
    }
};
struct BurnAway final : Kernel {
    const std::shared_ptr<const BurnGeometry> data;
    std::vector<uint32_t>                     F;
    uint32_t                                  ink, CHAR, SCORCH, WARM, SMOKE, LIT, DIM;
    Rng                                       rng;
    Particles                                 ps, ash;
    Lights                                    lights;
    double                                    t   = 0;
    size_t                                    pin = 0, pout = 0;
    bool                                      closed = false, popped = false;
    BurnAway(const PixelFxSettings& o, std::shared_ptr<const BurnGeometry> geometry)
        : data(std::move(geometry)),
          F(o.palette),
          ink(o.transition.color),
          CHAR(mix(ink, F[0], .45)),
          SCORCH(mix(ink, hex(0x3a2216), .6)),
          WARM(mix(F[4], F[5], .4)),
          SMOKE(mix(hex(0x8a7276), F[2], .2)),
          LIT(mix(hex(0x8a7276), F[3], .25)),
          DIM(SMOKE_RAMP[3]),
          rng(o.seed) {
        ps.sparks  = {F[7], F[6], F[4], F[2]};
        ash.sparks = {hex(0xa39cab), hex(0x7b7484), hex(0x5c5664), hex(0x433e4b)};
    }
    double kIn() const {
        const auto& A = data->A;
        return A.lo - 8 + (A.hi - A.lo + 8) * std::pow(std::max(0., t) / CLOSE, 1.25);
    }
    double kOut() const {
        const auto& B = data->B;
        return t < POP ? B.lo - 24 + 24 * ease::inQuad(clamp((t - SMOULDER) / (POP - SMOULDER)))
                       : B.lo + (B.hi - B.lo + 16) * ease::outQuad(clamp((t - POP) / (OPEN - POP)));
    }
    void shed(uint32_t i, bool inA) {
        const std::array<double, 6> rate = inA ? std::array<double, 6>{.006, .0014, .0006, .0024, .0012, .0004}
                                               : std::array<double, 6>{.006, .0005, .0004, .0012, .0004, .0004};
        const double                x = i % screen::W + .5, y = i / screen::W + .5, end = inA ? POP + .1 : 3 - .05;
        double                      r = rng.next();
        if ((r -= rate[0]) < 0) {
            const double life = std::min(rng.range(.18, .4), end - t);
            if (life > .08) {
                const double vx = rng.range(-8, 8), vy = -rng.range(25, 50), size = rng.range(.8, 1.7);
                ps.spawn(rng, ParticleKind::Fire, x, y - 3, vx, vy, life, 0, size);
            }
        } else if ((r -= rate[1]) < 0) {
            const double life = std::min(inA ? rng.range(.6, 1.3) : rng.range(.5, 1), end - t);
            if (life > .2) {
                const double vx = rng.range(-30, 30), vy = -rng.range(40, 100);
                const double size = rng.isChance(.25) ? 2 : 1;
                ps.spawn(rng, ParticleKind::Ember, x, y - 2, vx, vy, life, 0, size);
            }
        } else if ((r -= rate[2] + rate[3] * std::pow(double(data->heat[i]), 2)) < 0) {
            const int    n  = rng.integer(4, 6);
            const double vx = rng.range(-6, 6);
            const bool   up = (inA ? data->A : data->B).ny[i] < -.25;
            for (int j = 0; j < n; ++j) {
                const double life = std::min(up ? rng.range(.8, 1.3) : rng.range(.35, .65), end - (t + j * .05));
                if (life > .4) {
                    const double xx = x + rng.range(-3.5, 3.5), vxx = vx + rng.range(-3, 3), vyy = -rng.range(18, 34),
                                 size = rng.range(2.8, 4.8);
                    ps.spawn(rng, ParticleKind::Smoke, xx, y - 4 - j * 1.2, vxx, vyy, life, SMOKE, size, j * .05);
                }
            }
        } else if ((r -= rate[4]) < 0) {
            const double life = std::min(rng.range(.9, 1.6), end - t);
            if (life > .3) {
                const double vx = rng.range(-18, 18), vy = -rng.range(25, 60);
                ash.spawn(rng, ParticleKind::Ember, x, y - 2, vx, vy, life);
            }
        } else if ((r -= rate[5]) < 0) {
            const double radius = rng.range(20, 32), strength = rng.range(.14, .26), life = rng.range(.18, .4);
            lights.add(x, y - 4, radius, WARM, strength, life);
        }
    }
    void update(double dt) override {
        t += dt;
        const auto& A = data->A;
        const auto& B = data->B;
        if (t < COVER) {
            const double k = kIn();
            while (pin < A.order.size() && A.f[A.order[pin]] <= k) shed(A.order[pin++], true);
            if (!closed && t >= CLOSE) {
                closed         = true;
                const auto   i = A.order.back();
                const double x = i % screen::W, y = i / screen::W;
                lights.add(x, y, 64, WARM, .5, .4);
                for (int j = 0; j < 12; ++j) {
                    const double xx = x + rng.range(-6, 6), yy = y + rng.range(-3, 3), vx = rng.range(-50, 50),
                                 vy = -rng.range(40, 120), life = rng.range(.5, .75);
                    const double size = rng.isChance(.3) ? 2 : 1;
                    ps.spawn(rng, ParticleKind::Ember, xx, yy, vx, vy, life, 0, size);
                }
                for (int j = 0; j < 5; ++j) {
                    const double xx = x + rng.range(-8, 8), yy = y + rng.range(-4, 2), vx = rng.range(-10, 10),
                                 vy = -rng.range(12, 28), life = rng.range(1.1, 1.6), size = rng.range(3.5, 5.5),
                                 delay = rng.range(0, .15);
                    ps.spawn(rng, ParticleKind::Smoke, xx, yy, vx, vy, life, SMOKE, size, delay);
                }
            }
        }
        if (t >= SMOULDER && t < POP && rng.isChance(.1)) {
            const auto   i = B.order[size_t(rng.integer(0, 400))];
            const double x = i % screen::W + .5, y = i / screen::W + .5;
            if (rng.isChance(.6)) {
                const double vx = rng.range(-4, 4), vy = -rng.range(8, 16),
                             life = std::min(rng.range(.9, 1.4), 3 - .05 - t), size = rng.range(2, 3.5);
                ps.spawn(rng, ParticleKind::Smoke, x, y, vx, vy, life, SMOKE, size);
            } else {
                const double vx = rng.range(-12, 12), vy = -rng.range(15, 40), life = rng.range(.4, .8);
                ps.spawn(rng, ParticleKind::Ember, x, y, vx, vy, life);
            }
        }
        if (!popped && t >= POP) {
            popped = true;
            lights.add(data->ox, data->oy, 90, WARM, .6, .45);
            lights.add(data->ox, data->oy, 34, F[7], .7, .12);
            for (int j = 0; j < 12; ++j) {
                const double a = rng.range(0, TAU), sp = rng.range(50, 150), life = rng.range(.4, .9);
                const double size = rng.isChance(.3) ? 2 : 1;
                ps.spawn(rng, ParticleKind::Ember, data->ox + std::cos(a) * 3, data->oy + std::sin(a) * 2,
                         std::cos(a) * sp, std::sin(a) * sp * .7 - 40, life, 0, size);
            }
            for (int j = 0; j < 10; ++j) {
                const double a = rng.range(0, TAU), sp = rng.range(120, 260), life = rng.range(.15, .35);
                ps.spawn(rng, ParticleKind::Spark, data->ox, data->oy, std::cos(a) * sp, std::sin(a) * sp - 50, life);
            }
        }
        if (t >= POP && t < OPEN + .2) {
            const double k = kOut();
            while (pout < B.order.size() && B.f[B.order[pout]] <= k) shed(B.order[pout++], false);
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        ash.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawSmoke(Paint& p, Paint& p0) const {
        const bool   inPhase = t < COVER, open = t >= SMOULDER && t < OPEN + .25;
        const double k = inPhase ? kIn() : kOut();
        const auto&  f = inPhase ? data->A.f : data->B.f;
        for (const auto& q : ps.list) {
            if (q.kind != ParticleKind::Smoke || q.delay > 0) continue;
            const double kk = q.life / q.max, age = 1 - kk,
                         r = q.size * (.65 + std::min(1., age * 2.5) * .45 + age * .4);
            const size_t i = size_t(clamp(std::floor(q.y), 0, screen::H - 1)) * screen::W +
                             size_t(clamp(std::floor(q.x), 0, screen::W - 1));
            const double e = inPhase || open ? (inPhase ? k - f[i] : f[i] - k) : 99, lit = clamp(1 - std::abs(e) / 30);
            const double a = std::min(1., age * 12) * std::min(1., kk * 2.4) * .58 *
                             (e > 0     ? .05 + .95 * lit
                              : inPhase ? 1
                                        : .2 + .8 * lit);
            if (a < .04) continue;
            const auto c = mix(mix(DIM, LIT, lit), SMOKE_RAMP[2], age * .5);
            auto&      d = q.x - r > 0 && q.y - r > 0 && q.x + r < screen::W - 1 && q.y + r < screen::H - 1 ? p0 : p;
            d.disc(q.x, q.y, r, c, a * .45);
            d.disc(q.x + r * .05, q.y + r * .06, r * .74, c, a * .62);
            if (age < .5) d.disc(q.x - r * .22, q.y - r * .3, r * .5, mix(c, WHITE, .15), a * .4 * (1 - age * 2));
        }
    }
    void tongue(Paint& p, double x, double y, double h, double w, double sway, uint32_t n) const {
        constexpr std::array<std::array<double, 3>, 4> layers{{{1, 1, 2}, {.76, .86, 4}, {.54, .68, 6}, {.3, .42, 8}}};
        for (const auto& layer : layers) {
            const double lw = layer[0], lh = layer[1], Ht = h * lh;
            const auto   col = F[size_t(layer[2])];
            for (int r = 0; r <= Ht; ++r) {
                const double u    = r / std::max(1., Ht),
                             half = w * lw * std::pow(1 - u, .85) * std::min(1., (r + 1.5) / 3.5) +
                                    noise(t * 9 + r * .45, n) * .5;
                if (half < .3) continue;
                const double cx = x + sway * u * u * lh + noise(t * 4 + r * .2, n + 7) * u * 1.1;
                for (int px = int(jsRound(cx - half)); px <= int(jsRound(cx + half)); ++px)
                    p.px(px, jsRound(y - r), col);
            }
        }
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto                                        p = screen::clip(p0);
        std::array<uint32_t, screen::W * screen::H> line{};
        size_t                                      lineCount = 0;
        if (t >= COVER && t < SMOULDER)
            screen::rect(p0, 0, 0, screen::W, screen::H, ink);
        else if (t < OPEN + .25) {
            const bool              inPhase = t < COVER;
            const double            k       = inPhase ? kIn() : kOut();
            const auto&             field   = inPhase ? data->A : data->B;
            const auto&             f       = field.f;
            const auto&             g       = field.g;
            const double            fade    = inPhase ? 1 - smooth(COVER - .15, COVER, t) : 1;
            std::array<float, 64>   FL{}, HV{};
            std::array<uint8_t, 64> TW{};
            for (size_t j = 0; j < 64; ++j) {
                const double q = j / 63.;
                FL[j]          = float(.75 + .25 * std::sin(t * 7 + q * 9));
                HV[j]          = float(.3 * std::sin(t * 5.5 + q * 11));
                TW[j]          = std::sin(t * 13 + q * 40) > -.5 ? 1 : 0;
            }
            screen::Pixels px(p0);
            const auto&    ord = field.order;
            const size_t   N   = ord.size();
            const double   su = clamp((t - SMOULDER) / (POP - SMOULDER)), SR = 5 + 17 * su;
            const double   sk      = inPhase || t < SMOULDER ? 0 : t < POP ? .3 + .7 * su : 1 - (t - POP) / .3;
            const auto     shadeAt = [&](uint32_t i) {
                const double e = inPhase ? k - f[i] : f[i] - k, s = e * g[i];
                px.at(i);
                if (e < 0) {
                    if (inPhase && s > -2.5) px.blend(SCORCH, .5 * (1 + s / 2.5));
                    const double u = std::max(0., 1 + e / HALO), v = 1 + e / POOL;
                    px.add(WARM, (.3 * u * u + .12 * v * v) * FL[data->ph[i]]);
                    return;
                }
                const double hv = clamp(double(data->heat[i]) + double(HV[data->ph[i]])), th = .6 + .6 * hv, u = s / th;
                if (u < 6.4) {
                    if (u < .9) {
                        EV_ASSERT(lineCount < line.size(), "Source field slices visit each pixel once");
                        line[lineCount++] = i;
                    }
                    px.put(u < .9    ? (hv > .6 ? F[8] : F[7])
                           : u < 1.7 ? F[6]
                           : u < 2.4 ? F[5]
                           : u < 3.1 ? F[4]
                           : u < 3.8 ? F[3]
                           : u < 4.6 ? F[2]
                           : u < 5.5 ? F[1]
                                     : F[0]);
                    if (u < 4.6) px.add(WARM, .45 * (1 - u / 4.6) * (.7 + .3 * hv));
                    return;
                }
                if (sk > 0 && f[i] < data->B.lo + SR) {
                    const double hm = (1 - (f[i] - data->B.lo) / SR) * sk * (.7 + .6 * hv);
                    if (data->vein[i] < .04 + .08 * hm) {
                        px.put(hm > .95   ? F[6]
                               : hm > .75 ? F[5]
                               : hm > .55 ? F[4]
                               : hm > .38 ? F[3]
                               : hm > .22 ? F[2]
                                          : F[1]);
                        px.add(WARM, .4 * hm);
                        return;
                    }
                    px.add(WARM, .2 * hm * hm);
                }
                const double L = data->fleck[i] * (.8 + .4 * hv);
                if (L > 0 && s < L) {
                    const double v = (s - 6.4 * th) / std::max(1., L - 6.4 * th);
                    if (v < 1 && TW[(data->ph[i] * 5) & 63] && !(fade < 1 && fade <= data->jit[i] / 1.5 + .5)) {
                        px.put(v < .3 ? F[4] : v < .55 ? F[3] : v < .8 ? F[2] : F[1]);
                        px.add(WARM, .3 * (1 - v));
                        return;
                    }
                }
                if (e < 22) px.add(WARM, .2 * std::pow(1 - e / 22, 2));
                px.put(u - 6.4 < 1.6 * th + data->jit[i] ? CHAR : ink);
            };
            const size_t lo = field.below(k - (inPhase ? 33 : POOL)),
                         hi = field.below(std::max(k + (inPhase ? POOL : 33), sk > 0 ? data->B.lo + SR : -1e9));
            const size_t c0 = inPhase ? 0 : hi, c1 = inPhase ? lo : N;
            for (size_t j = c0; j < c1; ++j) {
                const auto i = ord[j];
                if (data->fleck[i])
                    shadeAt(i);
                else {
                    px.at(i);
                    px.put(ink);
                }
            }
            for (size_t j = lo; j < hi; ++j) shadeAt(ord[j]);
            for (size_t j = 0; j < lineCount; ++j) {
                const auto   i = line[j], x = i % screen::W, y = i / screen::W;
                const double dn = inPhase ? -double(field.ny[i]) : double(field.ny[i]),
                             h0 = rand({x, y >> 3, data->seed + 51});
                if (h0 > .12 + .6 * std::max(0., dn)) continue;
                const auto   n  = (x * 7 + (y >> 4) * 131) % 977;
                const double hv = data->heat[i], leap = 1 + std::max(0., noise(t * 2.6 + n * 3.1, n + 70) - .5) * 1.8;
                const double h = (5 + 7 * rand({x, 1, data->seed + 53})) * (.6 + .4 * noise(t * 3.3 + n * 1.7, n)) *
                                 (.6 + .6 * hv) * leap * fade * (dn > 0 ? .65 + .5 * dn : .5);
                if (h >= 1.5)
                    tongue(p, x + .5, y + 1, h, (1.1 + .7 * hv) * (.8 + h / 16), noise(t * 2.2 + n, n + 40) * 1.5, n);
            }
        }
        drawSmoke(p, p0);
        ash.draw(p, Paint::Layer::Front);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
struct BurnRecipe final : SimulationRecipe {
    const std::shared_ptr<const BurnGeometry> geometry;
    explicit BurnRecipe(const PixelFxSettings& s) : geometry(std::make_shared<const BurnGeometry>(s.seed)) {}
    std::unique_ptr<Kernel> create(const PixelFxSettings& s) const override {
        return std::make_unique<BurnAway>(s, geometry);
    }
};
}  // namespace
const Descriptor& burnAwayDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d = [] {
        Descriptor v{{PixelFxKind::BurnAway, "transitions/burn-away", 320, 180, 0, 0, 3, false},
                     9,
                     [](PixelFxSettings& s) {
                         s.seed             = 3;
                         s.transition.color = hex(0x0b0a10);
                         s.palette = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                      hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
                     },
                     nullptr,
                     options};
        v.prepareSimulation = [](const PixelFxSettings& s) -> std::unique_ptr<const SimulationRecipe> {
            return std::make_unique<BurnRecipe>(s);
        };
        return v;
    }();
    return d;
}
}  // namespace eve::stylize::pixelfx
