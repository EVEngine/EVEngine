// eve-hot-path
// Native adaptation of pixelfx scifi/warp-out.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double FLASH = .42, BREAK0 = .5, BREAK1 = 1.1, LIFT = 1.3, GONE = 1.62, TOP = -128;
struct Bit {
    double   x, y, x0, y0, vx, vy, tr;
    uint32_t color;
    bool     free;
    double   j;
    bool     rim;
};
struct WarpOut final : Kernel {
    const std::vector<uint32_t> C;
    Rng                         rng;
    Particles                   ps;
    Lights                      lights;
    std::vector<Bit>            bits;
    std::vector<Point>          edge;
    double                      t       = 0;
    bool                        flashed = false, lifted = false;
    explicit WarpOut(const PixelFxSettings& o) : C(o.palette), rng(o.seed) {
        const auto& A = o.actor;
        ps.sparks     = {WHITE, C[6], C[5], C[4]};
        const auto on = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < A.width && y < A.height &&
                   A.mask[size_t(y) * size_t(A.width) + size_t(x)] > 0;
        };
        for (int my = -1; my <= A.height; ++my)
            for (int mx = -1; mx <= A.width; ++mx) {
                if (on(mx, my)) {
                    const double   x = A.x + mx, y = A.y + my,
                                   tr  = BREAK0 + (double(my) / A.height) * (BREAK1 - BREAK0 - .12) + rng.range(0, .12);
                    const bool     rim = !on(mx - 1, my) || !on(mx + 1, my) || !on(mx, my - 1) || !on(mx, my + 1);
                    const uint32_t color = A.pixels[size_t(my) * size_t(A.width) + size_t(mx)];
                    const double   j     = rng.next();
                    bits.push_back({x, y, x, y, 0, 0, tr, color ? color : C[3], false, j, rim});
                } else if (on(mx - 1, my) || on(mx + 1, my) || on(mx, my - 1) || on(mx, my + 1))
                    edge.push_back({A.x + mx, A.y + my});
            }
    }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < FLASH ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    double width() const {
        return t < BREAK0 ? 0
               : t < LIFT ? 2 + 7 * ease::outCubic(clamp((t - BREAK0) / .35))
                          : lerp(9, .6, ease::inCubic(clamp((t - LIFT) / (GONE - LIFT))));
    }
    double foot() const { return t < LIFT ? 0 : lerp(0, TOP, ease::inQuad(clamp((t - LIFT) / (GONE - LIFT)))); }
    void   update(double dt) override {
        t += dt;
        if (t < FLASH - .05 && rng.isChance(.7)) {
            const double   a = rng.range(0, TAU), D = rng.range(26, 48), T = rng.range(.16, .26),
                           v = (D * 3) / (std::exp(3 * T) - 1), cy = -19;
            const uint32_t color = rng.isChance(.5) ? C[6] : C[5];
            ps.spawn(rng, ParticleKind::Streak, std::cos(a) * D, cy + std::sin(a) * D * .8, -std::cos(a) * v,
                     -std::sin(a) * v * .8, T, color);
        }
        if (t >= FLASH && !flashed) {
            flashed = true;
            lights.add(0, -20, 80, C[4], .9, .5);
            lights.add(0, -20, 30, WHITE, .8, .12);
            for (int i = 0; i < 14; ++i) {
                const double   x = rng.range(-14, 14), y = -rng.range(2, 40), vx = rng.range(-20, 20),
                               vy = -rng.range(10, 40), life = rng.range(.3, .6);
                const uint32_t color = rng.isChance(.5) ? WHITE : C[6];
                const double   size  = rng.range(.8, 1.8);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, color, size);
            }
        }
        for (size_t i = bits.size(); i > 0; --i) {
            auto& b = bits[i - 1];
            if (t < b.tr) continue;
            if (!b.free) {
                b.free          = true;
                const double ox = b.x + .5, oy = b.y + 19;
                const double first = rng.range(1.5, 4), second = rng.range(-10, 10), third = rng.range(.5, 1.5),
                             fourth = rng.range(10, 40);
                b.vx                = ox * first + second;
                b.vy                = oy * third - fourth;
            }
            b.vy -= 560 * dt;
            b.vx += (-(b.x + .5) * 14 - b.vx * 3) * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            if (b.y < TOP + 2) bits.erase(bits.begin() + std::ptrdiff_t(i - 1));
        }
        if (t >= LIFT && !lifted) {
            lifted = true;
            lights.add(0, -2, 40, C[4], .7, .3);
            for (int i = 0; i < 22; ++i) {
                const double a = -PI / 2 + rng.range(-1.4, 1.4), sp = rng.range(80, 220), x = rng.range(-3, 3),
                             life = rng.range(.2, .55);
                ps.spawn(rng, ParticleKind::Spark, x, -1, std::cos(a) * sp, std::sin(a) * sp, life);
            }
            for (int i = 0; i < 16; ++i) {
                const double   x = rng.range(-10, 10), y = -rng.range(4, 70), vx = rng.range(-6, 6),
                               vy = -rng.range(10, 30), life = rng.range(.5, .95);
                const uint32_t color = rng.isChance(.4) ? WHITE : C[6];
                const double   size = rng.range(.6, 1.6), delay = rng.range(0, .3);
                ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, color, size, delay);
            }
        }
        ps.update(dt, rng, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double                  k = clamp(t / FLASH), fade = 1 - smooth(LIFT, LIFT + .6, t);
        const std::array<uint32_t, 4> ramp{C[3], C[4], C[5], C[6]};
        if (fade > 0)
            band(p, 0, 0, lerp(30, 14, ease::outCubic(k)), .16, 1.5, (.4 + .6 * k) * fade, ramp,
                 [](double g) { return std::sin(g) < 0; });
        const double w = width(), f = foot();
        if (w > .3 && t < GONE)
            for (int y = int(std::floor(TOP)); y < f; ++y) {
                if (y >= f && t >= LIFT) break;
                for (int x = int(std::floor(-w)) - 1; x <= std::ceil(w); ++x) {
                    const double q = std::abs(x + .5) / w;
                    if (q > 1 + 1 / w) continue;
                    if (w < 2.2)
                        p.dot(x, y, q < .7 ? WHITE : C[6]);
                    else if (q > 1)
                        p.dot(x, y, C[4], .5);
                    else if (q > 1 - 1.2 / w)
                        p.dot(x, y, C[6]);
                    else
                        p.blend(x + .5, y + .5, C[5], .12 + .25 * q * q);
                }
                if ((y & 7) == 0) p.glow(0, y, std::min(w * 2.5 + 6, y - TOP + 2), C[4], .28);
            }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        const double                  k = clamp(t / FLASH), fade = 1 - smooth(LIFT, LIFT + .6, t);
        const std::array<uint32_t, 4> ramp{C[3], C[5], C[6], WHITE};
        if (fade > 0)
            band(p, 0, 0, lerp(30, 14, ease::outCubic(k)), .16, 1.5, (.4 + .6 * k) * fade, ramp,
                 [](double g) { return std::sin(g) >= 0; });
        if (t < FLASH) {
            const uint32_t fr = jsUint(std::floor(t * 24));
            for (const auto& point : edge) {
                const uint32_t x = jsUint(point.x), y = jsUint(point.y);
                if (rand({x, y, fr}) < .25 + .7 * k)
                    p.dot(point.x, point.y, k > .7 && rand({x, y, fr, 1}) < .5 ? WHITE : C[5]);
            }
            p.glow(0, -20, 26, C[4], .4 * k);
        }
        for (const auto& b : bits) {
            if (b.free || t < FLASH) continue;
            const double   a    = t - FLASH;
            const bool     scan = wrap(b.y + std::floor(t * 40), 4) == 0;
            const uint32_t c    = a < .07   ? WHITE
                                  : a < .16 ? C[6]
                                  : b.rim   ? C[6]
                                            : mix(b.color, scan ? C[6] : C[5], scan ? .6 : .45);
            p.dot(b.x, b.y, b.tr - t < .06 ? WHITE : c);
        }
        for (const auto& b : bits) {
            if (!b.free) continue;
            const double   sp = std::abs(b.vy), L = std::min(8., sp * .02), u = clamp((b.y - TOP) / (b.y0 - TOP));
            const uint32_t head = u > .75 ? WHITE : u > .4 ? C[6] : C[5];
            if (L >= 1 && b.j < .33)
                p.lineFn(
                    b.x, b.y, b.x - b.vx * .01, b.y + L,
                    [&](double s) { return s < .25  ? head
                                           : s < .6 ? C[5]
                                                    : C[3]; }, 1.);
            else if (b.j < .75 || u > .6)
                p.dot(b.x, b.y, b.j > .9 ? WHITE : head);
        }
        if (t >= FLASH && t < FLASH + .12) p.sparkle(0, -20, 10 * (1 - (t - FLASH) / .12), C[6], C[5], .6);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& warpOutDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::WarpOut, "scifi/warp-out", 128, 168, 64, 128, 2.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 4;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x0b1030), hex(0x152a63), hex(0x1d4f9e), hex(0x2582d2),
                              hex(0x36b9ee), hex(0x8ae8ff), hex(0xd4fbff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<WarpOut>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
