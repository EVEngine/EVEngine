// eve-hot-path
// Adapted from pixelfx explosions/cartoon-pow.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Explosion.h"
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size  = 1;
};
struct Spike {
    double r, j;
};
struct ActionLine {
    double angle, r, length, t0;
};
struct Letter {
    std::string text;
    int         width;
};
struct CartoonPow final : Kernel {
    std::vector<uint32_t>      C;
    Rng                        rng;
    Particles                  ps;
    std::array<Spike, 28>      spikes;
    std::array<ActionLine, 16> lines;
    std::vector<Letter>        letters;
    double                     startX = 0, t = 0;
    bool                       popped = false, burst = false;
    explicit CartoonPow(const PixelFxSettings& o) : C(o.palette), rng(o.seed) {
        ps.sparks = {C[6], C[5], C[4], C[3]};
        for (size_t i = 0; i < spikes.size(); ++i)
            spikes[i] = {i % 2 ? rng.range(.5, .6) : rng.range(.85, 1.05), rng.next() * 100};
        for (size_t i = 0; i < lines.size(); ++i)
            lines[i] = {(i + rng.range(.2, .8)) / 16 * TAU, rng.range(.92, 1.08), rng.range(10, 17), rng.range(0, .06)};
        size_t offset = 0;
        double width  = -3;
        eachCodepoint(o.explosion.text, [&](uint32_t code) {
            const size_t bytes = code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4;
            auto         ch    = o.explosion.text.substr(offset, bytes);
            offset += bytes;
            const int w = textWidth(ch, PixelFont::Big) * 3;
            letters.push_back({std::move(ch), w});
            width += w + 3;
        });
        startX = -width / 2;
    }
    void emit(Emission q) { ps.spawn(rng, q.kind, q.x, q.y, q.vx, q.vy, q.life, q.color, q.size); }
    void update(double dt) override {
        t += dt;
        if (!burst && t >= .05) {
            burst = true;
            for (int i = 0; i < 22; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(180, 360);
                emit({ParticleKind::Spark, std::cos(a) * 14, std::sin(a) * 10, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.12, .3)});
            }
        }
        if (!popped && t >= .66) {
            popped = true;
            const std::array<uint32_t, 3> ramp{C[6], C[5], C[4]};
            for (int i = 0; i < 14; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(60, 150);
                emit({ParticleKind::Star, std::cos(a) * 26, std::sin(a) * 18, std::cos(a) * sp, std::sin(a) * sp,
                      rng.range(.25, .42), rng.pick<uint32_t>(ramp), rng.range(1.2, 2.4)});
            }
        }
        ps.update(dt, rng);
        explosion::cull(ps, -89, -69, 89, 69);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const double k     = clamp(t / .05),
                     s     = t < .05   ? .25 + .95 * ease::outCubic(k)
                             : t < .66 ? 1 + .18 * std::exp(-(t - .05) * 18) * std::cos((t - .05) * 40)
                                       : 1 - ease::inBack(clamp((t - .66) / (.8 - .66)), 2),
                     shake = t < .3 ? (1 - t / .3) * 2.2 : 0, sx = jsRound(std::sin(t * 97) * shake),
                     sy = jsRound(std::cos(t * 83) * shake);
        if (s > .02) {
            const uint32_t         boil  = jsUint(std::floor(t * 12));
            const double           outer = 50 * s, rot = -.1 + (t > .66 ? (t - .66) * 3 : 0);
            std::array<double, 28> rad;
            for (size_t i = 0; i < rad.size(); ++i)
                rad[i] = spikes[i].r * (1 + (rand({uint32_t(i), boil, 7}) - .5) * .08);
            p.shader(sx - outer - 4, sy - outer - 4, sx + outer + 4, sy + outer + 4,
                     [&](double x, double y, ShadeOutput& out) {
                         const double dx = x - sx, dy = (y - sy) / .82, d = jsHypot2(dx, dy),
                                      u = wrap((std::atan2(dy, dx) - rot) / TAU, 1) * 28;
                         const size_t i = size_t(std::floor(u)) % 28;
                         const double f = u - std::floor(u), r = rad[i] + (rad[(i + 1) % 28] - rad[i]) * f, e = d / r;
                         if (e > outer + 2.5) return;
                         if (e > outer) {
                             out.c = C[0];
                             return;
                         }
                         if (e > outer * .66 + 2) {
                             out.c = dx * .6 + dy * .8 > outer * .25 &&
                                             (int(std::floor(x)) + int(std::floor(y)) * 2) % 4 == 0 &&
                                             int(std::floor(y)) % 2 == 0
                                         ? C[4]
                                         : C[5];
                             return;
                         }
                         if (e > outer * .66) {
                             out.c = C[0];
                             return;
                         }
                         out.c = e > outer * .44 ? C[3] : C[4];
                     });
            p.glow(sx, sy, outer * 1.3, C[5], .3 * std::min(1., s));
        }
        for (const auto& q : lines) {
            const double u = (t - .05 - q.t0) / .32;
            if (u <= 0 || u >= 1) continue;
            const double r0 = (46 + 22 * ease::outCubic(u)) * q.r, L = q.length * .7 * (1 - u), ca = std::cos(q.angle),
                         sa = std::sin(q.angle) * .82;
            p.lineFn(
                ca * r0, sa * r0, ca * (r0 + L), sa * (r0 + L), [&](double v) { return v < .5 ? WHITE : C[5]; }, 1.);
        }
        const double ru = (t - .05) / .24;
        if (ru > 0 && ru < 1)
            p.ring(0, 0, 30 + 34 * ease::outCubic(ru), std::max(1., 2.5 * (1 - ru)), ru < .4 ? WHITE : C[5], 1.1 - ru);
        double x = startX;
        for (size_t i = 0; i < letters.size(); ++i) {
            const auto&  ch = letters[i];
            const double u = (t - .05 - .04 - i * .05) / .12, away = clamp((t - .66) / (.8 - .66 - .04));
            if (u > 0 && away < 1) {
                const double drop = (1 - ease::outBack(clamp(u), 2.4)) * -14,
                             y = jsRound(-11 + drop - i * 1.5 + sy + away * 6), lx = jsRound(x + sx), a = 1 - away;
                TextOptions  opts;
                opts.font  = PixelFont::Big;
                opts.scale = 3;
                opts.alpha = a;
                for (int d = 4; d >= 1; --d) {
                    opts.outline = d == 4 ? std::optional<uint32_t>(C[0]) : std::nullopt;
                    p.text(ch.text, lx + d, y + d, d >= 3 ? C[0] : C[1], opts);
                }
                opts.outline.reset();
                for (const auto& off :
                     std::array<Point, 8>{{{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}})
                    p.text(ch.text, lx + off.x, y + off.y, C[0], opts);
                p.text(ch.text, lx, y, [&](int, int gy) { return gy < 4 ? C[6] : gy < 6 ? C[5] : C[4]; }, opts);
            }
            x += ch.width + 3;
        }
        ps.draw(p);
        if (t > .05 && t < .05 + .12) p.sparkle(sx + 34, sy - 22, 6 * (1 - (t - .05) / .12), C[6], C[5], .6);
    }
};
}  // namespace
const Descriptor& cartoonPowDescriptor() {
    static const std::array<Option, 1> options{{{"text", 0, 64, false, 0, &PixelFxExplosionSettings::text}}};
    static const Descriptor            descriptor{
        {PixelFxKind::CartoonPow, "explosions/cartoon-pow", 184, 144, 92, 72, 1.05, false},
        7,
        [](PixelFxSettings& s) {
            s.seed           = 5;
            s.palette        = {hex(0x1a0e14), hex(0x6e1a12), hex(0xc8361a), hex(0xf47a24),
                                hex(0xffc23c), hex(0xfff07a), WHITE};
            s.explosion.text = "POW!";
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CartoonPow>(s); }};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
