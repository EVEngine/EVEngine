// eve-hot-path
// Native adaptation of pixelfx ui/heart-lost.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/UiWord.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                           HIT = .14, CRACK = .24, SPLIT = .5, OX = -9.5, OY = -8, END = 1.5 - .05;
constexpr std::array<std::string_view, 16> MASK{
    {"....xxxx...xxxx....", "..xxxxxxx.xxxxxxx..", ".xxxxxxxxxxxxxxxxx.", "xxxxxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxxxxx",
     "xxxxxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxxxxx", ".xxxxxxxxxxxxxxxxx.", "..xxxxxxxxxxxxxxx..", "...xxxxxxxxxxxxx...",
     "....xxxxxxxxxxx....", ".....xxxxxxxxx.....", "......xxxxxxx......", ".......xxxxx.......", "........xxx........",
     ".........x........."}};
constexpr std::array<int, 16> CRACK_X{9, 9, 10, 9, 8, 8, 9, 10, 10, 9, 8, 9, 9, 10, 9, 9};
bool                          inMask(int i, int j) {
    return i >= 0 && j >= 0 && i < 19 && j < 16 && size_t(i) < MASK[size_t(j)].size() &&
           MASK[size_t(j)][size_t(i)] == 'x';
}
struct HeartHalf {
    double side, x, y, vx, vy, angle, angularVelocity;
};
struct HeartLost final : Kernel {
    const std::vector<uint32_t>   C;
    Rng                           R;
    const uint32_t                seed;
    const std::array<uint32_t, 3> socket{hex(0x0d0a14), hex(0x2a2338), hex(0x3c3350)};
    std::array<HeartHalf, 2>      halves;
    Particles                     ps;
    Bits                          bits;
    Lights                        lights;
    double                        t   = 0;
    bool                          hit = false, split = false, gone = false;
    explicit HeartLost(const PixelFxSettings& o) : C(o.palette), R(o.seed), seed(o.seed) {
        ps.sparks = {C[7], C[6], C[5], C[3]};
        size_t i  = 0;
        for (double side : {-1., 1.}) {
            const double vx = side * R.range(55, 70), vy = -R.range(60, 80), va = side * R.range(2.2, 3.2);
            halves[i++] = {side, side * 4.5, 0, vx, vy, 0, va};
        }
    }
    uint32_t shadeAt(int i, int j) const {
        const double u = (i - 9) / 9.5, v = double(j - 7) / 8;
        if ((i == 3 || i == 4) && (j == 2 || j == 3) && !(i == 4 && j == 3)) return WHITE;
        if (i >= 2 && i <= 5 && j >= 2 && j <= 5 && u + v < -.75) return C[5];
        const double l = -(u * .55 + v * .65);
        return l > .25 ? C[4] : l > -.3 ? C[3] : l > -.6 ? C[2] : C[1];
    }
    void drawPart(Paint& p, int side, double x, double y, double angle, bool white, double crackK, double alpha) const {
        const double ca = std::cos(angle), sa = std::sin(angle), px0 = side < 0 ? -4.5 : side > 0 ? 4.5 : 0;
        const auto   own = [&](int i, int j) {
            return inMask(i, j) && (side == 0 || (side < 0 ? i < CRACK_X[size_t(j)] : i >= CRACK_X[size_t(j)]));
        };
        p.shader(x - 13, y - 13, x + 13, y + 13, [&](double sx, double sy, ShadeOutput& out) {
            const double dx = sx - x, dy = sy - y, lx = dx * ca + dy * sa + px0, ly = -dx * sa + dy * ca;
            const int    i = int(std::floor(lx - OX)), j = int(std::floor(ly - OY));
            if (!own(i, j)) return;
            out.a           = alpha;
            const bool edge = !own(i - 1, j) || !own(i + 1, j) || !own(i, j - 1) || !own(i, j + 1);
            if (white) {
                out.c = edge ? C[6] : WHITE;
                return;
            }
            if (edge) {
                out.c = C[0];
                return;
            }
            if (side == 0 && double(j) / 16 < crackK) {
                if (i == CRACK_X[size_t(j)]) {
                    out.c = C[0];
                    return;
                }
                if (i == CRACK_X[size_t(j)] - 1) {
                    out.c = C[6];
                    return;
                }
            }
            out.c = shadeAt(i, j);
        });
    }
    void update(double dt) override {
        t += dt;
        if (!hit && t >= HIT) {
            hit = true;
            lights.add(0, 0, 40, C[5], .6, .3);
            for (int i = 0; i < 8; ++i) {
                const double a = R.range(0, TAU), speed = R.range(80, 170), life = R.range(.1, .22);
                ps.spawn(R, ParticleKind::Spark, std::cos(a) * 9, std::sin(a) * 7, std::cos(a) * speed,
                         std::sin(a) * speed - 30, life);
            }
        }
        if (!split && t >= SPLIT) {
            split = true;
            lights.add(0, 0, 44, WHITE, .6, .12);
            lights.add(0, 0, 50, C[5], .5, .4);
            const std::array<uint32_t, 4> colors{C[6], C[5], C[4], C[3]};
            for (int i = 0; i < 12; ++i) {
                const int    j = R.integer(1, 14);
                const double a = -PI / 2 + R.range(-1.4, 1.4), speed = R.range(40, 100);
                Debris       b;
                b.kind            = DebrisKind::Shard;
                b.x               = CRACK_X[size_t(j)] + OX;
                b.y               = j + OY;
                b.vx              = std::cos(a) * speed;
                b.vy              = std::sin(a) * speed;
                b.gravity         = .5;
                b.drag            = 1;
                b.life            = R.range(.35, .55);
                b.angle           = R.range(0, TAU);
                b.angularVelocity = R.range(-20, 20);
                b.size            = R.range(1.5, 3);
                b.color           = R.pick<uint32_t>(colors);
                bits.add(std::move(b));
            }
            for (int i = 0; i < 5; ++i) {
                const double x = R.range(-2, 2), y = R.range(-6, 6), vx = R.range(-25, 25), vy = R.range(-20, 5),
                             life = R.range(.35, .6), size = R.range(2, 3.5);
                ps.spawn(R, ParticleKind::Dust, x, y, vx, vy, life, C[2], size);
            }
            const std::array<uint32_t, 2> stars{C[7], C[6]};
            for (int i = 0; i < 6; ++i) {
                const double a = R.range(0, TAU), speed = R.range(30, 70), life = R.range(.25, .45);
                const auto   color = R.pick<uint32_t>(stars);
                ps.spawn(R, ParticleKind::Star, std::cos(a) * 4, std::sin(a) * 4, std::cos(a) * speed,
                         std::sin(a) * speed, life, color, i % 2 ? .6 : 1.4);
            }
        }
        if (split && !gone && t >= .95) {
            gone = true;
            const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
            for (const auto& h : halves)
                for (int i = 0; i < 5; ++i) {
                    const double a = R.range(0, TAU), speed = R.range(20, 50),
                                 life  = std::min(R.range(.2, .35), END - t);
                    const auto   color = R.pick<uint32_t>(colors);
                    ps.spawn(R, ParticleKind::Star, h.x + std::cos(a) * 4, h.y + std::sin(a) * 4, std::cos(a) * speed,
                             std::sin(a) * speed - 10, life, color, i ? .6 : 1.3);
                }
        }
        if (split)
            for (auto& h : halves) {
                h.vy += 520 * dt;
                h.vx -= h.vx * .6 * dt;
                h.x += h.vx * dt;
                h.y += h.vy * dt;
                h.angle += h.angularVelocity * dt;
                if (t < .92 && R.isChance(.05)) {
                    const double vx = R.range(-10, 10), vy = R.range(-10, 5), life = std::min(R.range(.3, .5), END - t);
                    ps.spawn(R, ParticleKind::Ember, h.x, h.y, vx, vy, life, C[4], 1);
                }
            }
        bits.update(dt);
        ps.update(dt, R, [](double) { return INFINITY; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p) const override {
        const auto   jitter  = ui::shake(t, 3, HIT, .25, seed);
        const double tremble = t > CRACK && t < SPLIT ? jsRound(std::sin(t * 90)) : 0,
                     sk      = seg(t, SPLIT - .05, SPLIT + .1) * (1 - seg(t, 1.15, 1.4));
        if (sk > 0)
            p.shader(-12, -10, 12, 10, [&](double sx, double sy, ShadeOutput& out) {
                const int i = int(std::floor(sx - OX)), j = int(std::floor(sy - OY));
                if (!inMask(i, j)) return;
                out.a           = sk;
                const bool edge = !inMask(i - 1, j) || !inMask(i + 1, j) || !inMask(i, j - 1) || !inMask(i, j + 1);
                out.c           = edge ? socket[2] : j < 3 ? socket[1] : socket[0];
            });
        if (t < SPLIT) {
            const double beat  = t < HIT ? 1 + .08 * std::max(0., std::sin(t * 22)) : 1;
            const bool   white = t >= HIT && t < HIT + .07;
            drawPart(p, 0, jitter.x + tremble, jitter.y, 0, white, seg(t, CRACK, SPLIT - .06), 1);
            p.glow(0, 0, 18 * beat, C[4], .3 + (white ? .5 : 0));
            if (t < HIT) p.sparkle(-6, -5, 2.5 * std::max(0., std::sin(t * 22)), C[7], C[6], .3);
        } else {
            const double fade  = 1 - seg(t, .9, 1);
            const bool   white = t < SPLIT + .05;
            if (fade > 0)
                for (const auto& h : halves) drawPart(p, int(h.side), h.x, h.y, h.angle, white, 0, fade);
            const double rk = (t - SPLIT) / .3;
            if (rk < 1)
                p.ring(0, 0, 8 + 16 * ease::outCubic(rk), std::max(1., 2 * (1 - rk)), rk < .3 ? WHITE : C[5], 1.2 - rk);
        }
        bits.draw(p);
        ps.draw(p);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& heartLostDescriptor() {
    static const Descriptor d{
        {PixelFxKind::HeartLost, "ui/heart-lost", 96, 88, 48, 34, 1.5, false},
        8,
        [](PixelFxSettings& s) {
            s.seed    = 5;
            s.palette = {hex(0x1a0508), hex(0x4a0b12), hex(0x8c1420), hex(0xd42a2e),
                         hex(0xff5a4a), hex(0xff9a86), hex(0xffd8cc), WHITE};
        },
        nullptr,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<HeartLost>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
